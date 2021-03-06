/// \file stream.cpp
/// Utilities for handling streams.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include "json.h"
#include "stream.h"
#include "procs.h"
#include "config.h"
#include "socket.h"
#include "defines.h"
#include "shared_memory.h"

std::string Util::getTmpFolder() {
  std::string dir;
  char * tmp_char = 0;
  if (!tmp_char) {
    tmp_char = getenv("TMP");
  }
  if (!tmp_char) {
    tmp_char = getenv("TEMP");
  }
  if (!tmp_char) {
    tmp_char = getenv("TMPDIR");
  }
  if (tmp_char) {
    dir = tmp_char;
    dir += "/mist";
  } else {
#if defined(_WIN32) || defined(_CYGWIN_)
    dir = "C:/tmp/mist";
#else
    dir = "/tmp/mist";
#endif
  }
  if (access(dir.c_str(), 0) != 0) {
    mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO); //attempt to create mist folder - ignore failures
  }
  return dir + "/";
}


/// Filters the streamname, removing invalid characters and converting all
/// letters to lowercase. If a '?' character is found, everything following
/// that character is deleted. The original string is modified.
void Util::Stream::sanitizeName(std::string & streamname) {
  //strip anything that isn't numbers, digits or underscores
  for (std::string::iterator i = streamname.end() - 1; i >= streamname.begin(); --i) {
    if (*i == '?') {
      streamname.erase(i, streamname.end());
      break;
    }
    if (!isalpha(*i) && !isdigit(*i) && *i != '_') {
      streamname.erase(i);
    } else {
      *i = tolower(*i);
    }
  }
}

bool Util::Stream::getLive(std::string streamname) {
  JSON::Value ServConf = JSON::fromFile(getTmpFolder() + "streamlist");
  std::string bufferTime;
  std::string debugLvl;
  std::string player_bin = Util::getMyPath() + "MistInBuffer";
  DEBUG_MSG(DLVL_WARN, "Starting %s -p -s %s", player_bin.c_str(), streamname.c_str());
  char * argv[15] = {(char *)player_bin.c_str(), (char *)"-p", (char *)"-s", (char *)streamname.c_str()};
  int argNum = 3;
  if (ServConf["streams"][streamname].isMember("DVR")) {
    bufferTime = ServConf["streams"][streamname]["DVR"].asString();
    argv[++argNum] = (char *)"-b";
    argv[++argNum] = (char *)bufferTime.c_str();
  }
  if (Util::Config::printDebugLevel != DEBUG){
    debugLvl = JSON::Value((long long)Util::Config::printDebugLevel).asString();
    argv[++argNum] = (char *)"--debug";
    argv[++argNum] = (char *)debugLvl.c_str();
  }
  argv[++argNum] = (char *)0;

  int pid = fork();
  if (pid == -1) {
    FAIL_MSG("Forking process for stream %s failed: %s", streamname.c_str(), strerror(errno));
    return false;
  }
  if (pid == 0){
    execvp(argv[0], argv);
    FAIL_MSG("Starting process %s for stream %s failed: %s", argv[0], streamname.c_str(), strerror(errno));
    _exit(42);
  }
  return true;
}

/// Starts a process for a VoD stream.
bool Util::Stream::getVod(std::string filename, std::string streamname) {
  std::string player_bin = Util::getMyPath() + "MistInDTSC";
  bool selected = false;
  if (filename.substr(filename.size() - 5) == ".dtsc") {
    player_bin = Util::getMyPath() + "MistInDTSC";
    selected = true;
  }
  if (filename.substr(filename.size() - 4) == ".flv") {
    player_bin = Util::getMyPath() + "MistInFLV";
    selected = true;
  }
  INFO_MSG("Starting %s -p -s %s %s", player_bin.c_str(), streamname.c_str(), filename.c_str());
  char * argv[15] = {(char *)player_bin.c_str(), (char *)"-p", (char *)"-s", (char *)streamname.c_str(), (char *)filename.c_str()};
  int argNum = 4;
  std::string debugLvl;
  if (Util::Config::printDebugLevel != DEBUG){
    debugLvl = JSON::Value((long long)Util::Config::printDebugLevel).asString();
    argv[++argNum] = (char *)"--debug";
    argv[++argNum] = (char *)debugLvl.c_str();
  }
  argv[++argNum] = (char *)0;
  
  int pid = fork();
  if (pid == -1) {
    FAIL_MSG("Forking process for stream %s failed: %s", streamname.c_str(), strerror(errno));
    return false;
  }
  if (pid == 0){
    execvp(argv[0], argv);
    FAIL_MSG("Starting process %s for stream %s failed: %s", argv[0], streamname.c_str(), strerror(errno));
    _exit(42);
  }
  return true;
}

/// Probe for available streams. Currently first VoD, then Live.
bool Util::Stream::getStream(std::string streamname) {
  sanitizeName(streamname);
  JSON::Value ServConf = JSON::fromFile(getTmpFolder() + "streamlist");
  if (ServConf["streams"].isMember(streamname)) {
    //check if the stream is already active, if yes, don't re-activate
    IPC::semaphore playerLock(std::string("/lock_" + streamname).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (!playerLock.tryWait()) {
      playerLock.close();
      DEBUG_MSG(DLVL_MEDIUM, "Playerlock for %s already active - not re-activating stream", streamname.c_str());
      return true;
    }
    playerLock.post();
    playerLock.close();
    if (ServConf["streams"][streamname]["source"].asString()[0] == '/') {
      DEBUG_MSG(DLVL_MEDIUM, "Activating VoD stream %s", streamname.c_str());
      return getVod(ServConf["streams"][streamname]["source"].asString(), streamname);
    } else {
      DEBUG_MSG(DLVL_MEDIUM, "Activating live stream %s", streamname.c_str());
      return getLive(streamname);
    }
  }
  DEBUG_MSG(DLVL_ERROR, "Stream not found: %s", streamname.c_str());
  return false;
}

/// Create a stream on the system.
/// Filters the streamname, removing invalid characters and
/// converting all letters to lowercase.
/// If a '?' character is found, everything following that character is deleted.
Socket::Server Util::Stream::makeLive(std::string streamname) {
  sanitizeName(streamname);
  std::string loc = getTmpFolder() + "stream_" + streamname;
  //create and return the Socket::Server
  return Socket::Server(loc);
}
