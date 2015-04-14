#include "dtsc.h"
#include <memory>
#include <iostream>

template<typename T>
std::unique_ptr<T> uptr(T* p) {
	return std::unique_ptr<T>(p);
}

void usage(const char *progname) {
	std::cout << "Usage: " << progname << " input" << std::endl;
}

int main(int argc, char const* argv[]) {
	if (argc != 2) {
		usage(argv[0]);
		return 1;
	}
	auto const input = argv[1];

	auto f = uptr(new DTSC::File(input));
	int bPos = 0;
	f->seek_bpos(0);
	f->parseNext();
	while (f->getPacket()) {
		switch (f->getPacket().getVersion()){
		case DTSC::DTSC_V1: {
			std::cout << "DTSCv1 packet: " << f->getPacket().getScan().toPrettyString() << std::endl;
			break;
		}
		case DTSC::DTSC_V2: {
			std::cout << "DTSCv2 packet (Track " << f->getPacket().getTrackId() << ", time " << f->getPacket().getTime() << "): " << f->getPacket().getScan().toPrettyString() << std::endl;
			break;
		}
		case DTSC::DTSC_HEAD: {
			std::cout << "DTSC header: " << f->getPacket().getScan().toPrettyString() << std::endl;
			break;
		}
		default:
			std::cout << "Invalid dtsc packet @ bpos " << bPos << std::endl;
			break;
		}
		bPos = f->getBytePos();
		f->parseNext();
	}

	return 0;
}