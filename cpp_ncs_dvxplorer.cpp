#include <libcaercpp/devices/dvxplorer.hpp>

#include <atomic>
#include <csignal>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <arpa/inet.h>
using namespace std;

#define IP "172.16.222.30"
#define PORT 3330
#define NUM_EVENTS 256 // Number of events to send

static atomic_bool globalShutdown(false);

static void globalShutdownSignalHandler(int signal) {
	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	if (signal == SIGTERM || signal == SIGINT) {
		globalShutdown.store(true);
	}
}

static void usbShutdownHandler(void *ptr) {
	(void) (ptr); // UNUSED.

	globalShutdown.store(true);
}

int main(void) {
	struct sigaction shutdownAction;

	shutdownAction.sa_handler = &globalShutdownSignalHandler;
	shutdownAction.sa_flags   = 0;
	sigemptyset(&shutdownAction.sa_mask);
	sigaddset(&shutdownAction.sa_mask, SIGTERM);
	sigaddset(&shutdownAction.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdownAction, NULL) == -1) {
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdownAction, NULL) == -1) {
		libcaer::log::log(libcaer::log::logLevel::CRITICAL, "ShutdownAction",
			"Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	// Open a DVS, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	auto handle = libcaer::devices::dvXplorer(1);

	// Let's take a look at the information we have on the device.
	auto info = handle.infoGet();

	printf("%s --- ID: %d, DVS X: %d, DVS Y: %d, Firmware: %d, Logic: %d.\n", info.deviceString, info.deviceID,
		info.dvsSizeX, info.dvsSizeY, info.firmwareVersion, info.logicVersion);

	// Send the default configuration before using the device.
	// No configuration is sent automatically!
	handle.sendDefaultConfig();

	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	handle.dataStart(nullptr, nullptr, nullptr, &usbShutdownHandler, nullptr);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	handle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);


	handle.configSet(CAER_HOST_CONFIG_PACKETS, CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, 128);
	handle.configSet(CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BUFFER_SIZE, 1024);
	handle.configSet(CAER_HOST_CONFIG_PACKETS,CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, 100);


	handle.configSet(DVX_DVS_CHIP, DVX_DVS_CHIP_DTAG_CONTROL, DVX_DVS_CHIP_DTAG_CONTROL_STOP);
	handle.configSet(DVX_DVS_CHIP, DVX_DVS_CHIP_FIXED_READ_TIME_ENABLE, false);
	handle.configSet(DVX_DVS_CHIP, DVX_DVS_CHIP_TIMING_ED, 1);
	handle.configSet(DVX_DVS_CHIP, DVX_DVS_CHIP_TIMING_NEXT_SEL, 5);
	handle.configSet(DVX_DVS_CHIP, DVX_DVS_CHIP_DTAG_CONTROL, DVX_DVS_CHIP_DTAG_CONTROL_START);



    int pShift = 15;
    int yShift = 0;
    int xShift = 16;
    unsigned int noTimestamp = 0x80000000;
    int sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(IP);




    unsigned int *eventArray = (unsigned int *)malloc(NUM_EVENTS * sizeof(unsigned int));
    if (eventArray == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
	unsigned int *empty = (unsigned int *)malloc(NUM_EVENTS * sizeof(unsigned int));
    if (empty == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

	for (int i = 0; i < NUM_EVENTS; i++) {
		eventArray[i] = 0;
		empty[i] = 0;
	}


	int udp_ev_counter = 0;
	while (!globalShutdown.load(memory_order_relaxed)) {
		std::unique_ptr<libcaer::events::EventPacketContainer> packetContainer = handle.dataGet();
		if (packetContainer == nullptr) {
			continue; // Skip if nothing there.
		}

		for (auto &packet : *packetContainer) {
			if (packet == nullptr) {
				printf("Packet is empty (not present).\n");
				continue; // Skip if nothing there.
			}

			if (packet->getEventType() == POLARITY_EVENT) {
				std::shared_ptr<const libcaer::events::PolarityEventPacket> polarity
					= std::static_pointer_cast<libcaer::events::PolarityEventPacket>(packet);

				// Get full timestamp and addresses of first event.

				for (int i = 0; i < packet->getEventNumber(); i++){
					const libcaer::events::PolarityEvent &firstEvent = (*polarity)[i];

					uint16_t x = firstEvent.getX();
					uint16_t y = firstEvent.getY();
					bool pol = true;
					if(x < 280 && y < 180){
						eventArray[udp_ev_counter] = noTimestamp + (pol << pShift) + (y << yShift) + (x << xShift);
						// printf("...\n");	
						udp_ev_counter++;
						if (udp_ev_counter == NUM_EVENTS){
							sendto(sock, eventArray, NUM_EVENTS * sizeof(unsigned int), 0, (struct sockaddr *)&server, sizeof(server));
							memcpy(eventArray, empty, NUM_EVENTS * sizeof(unsigned int));
							udp_ev_counter = 0;
						}
						// printf("(%u,%u)\n", x, y);	

					}
					
				}

				// int b = x << 2;

			}
		}
	}

	handle.dataStop();


	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);
}
