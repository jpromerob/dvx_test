#include <libcaer/devices/dvxplorer.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <stdatomic.h>


#define IP "172.16.222.30"
#define PORT 3330
#define NUM_EVENTS 8 // Number of events to send

// static atomic_bool globalShutdown(false);

static atomic_bool globalShutdown = ATOMIC_VAR_INIT(false);

static void globalShutdownSignalHandler(int signal) {
	// Simply set the running flag to false on SIGTERM and SIGINT (CTRL+C) for global shutdown.
	if (signal == SIGTERM || signal == SIGINT) {
		atomic_store(&globalShutdown, true);
	}
}


static void usbShutdownHandler(void *ptr) {
	(void) (ptr); // UNUSED.

	atomic_store(&globalShutdown, true);
}

int main(void) {

    printf("Whatever\n");


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

	struct sigaction shutdownAction;

	shutdownAction.sa_handler = &globalShutdownSignalHandler;
	shutdownAction.sa_flags   = 0;
	sigemptyset(&shutdownAction.sa_mask);
	sigaddset(&shutdownAction.sa_mask, SIGTERM);
	sigaddset(&shutdownAction.sa_mask, SIGINT);

	if (sigaction(SIGTERM, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGTERM. Error: %d.", errno);
		return (EXIT_FAILURE);
	}

	if (sigaction(SIGINT, &shutdownAction, NULL) == -1) {
		caerLog(CAER_LOG_CRITICAL, "ShutdownAction", "Failed to set signal handler for SIGINT. Error: %d.", errno);
		return (EXIT_FAILURE);
	}


	// Open a DAVIS, give it a device ID of 1, and don't care about USB bus or SN restrictions.
	caerDeviceHandle dvxplorer_handle = caerDeviceOpen(1, CAER_DEVICE_DVXPLORER, 0, 0, NULL);
	if (dvxplorer_handle == NULL) {
		return (EXIT_FAILURE);
	}


	// Let's take a look at the information we have on the device.
	struct caer_dvx_info dvxplorer_info = caerDVXplorerInfoGet(dvxplorer_handle);


	printf("ID: %d, DVS X: %d, DVS Y: %d\n",
		dvxplorer_info.deviceID, 
        dvxplorer_info.dvsSizeX, 
        dvxplorer_info.dvsSizeY);


	caerDeviceSendDefaultConfig(dvxplorer_handle);


	// Now let's get start getting some data from the device. We just loop in blocking mode,
	// no notification needed regarding new events. The shutdown notification, for example if
	// the device is disconnected, should be listened to.
	caerDeviceDataStart(dvxplorer_handle, NULL, NULL, NULL, &usbShutdownHandler, NULL);

	// Let's turn on blocking data-get mode to avoid wasting resources.
	caerDeviceConfigSet(dvxplorer_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BLOCKING, true);

	caerDeviceConfigSet(dvxplorer_handle, CAER_HOST_CONFIG_PACKETS, CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_PACKET_SIZE, 128);
	caerDeviceConfigSet(dvxplorer_handle, CAER_HOST_CONFIG_DATAEXCHANGE, CAER_HOST_CONFIG_DATAEXCHANGE_BUFFER_SIZE, 1024);
	caerDeviceConfigSet(dvxplorer_handle, CAER_HOST_CONFIG_PACKETS,CAER_HOST_CONFIG_PACKETS_MAX_CONTAINER_INTERVAL, 100);


	// According to: https://gitlab.com/inivation/dv/dv-runtime/-/blob/master/modules/cameras/dvxplorer.cpp?ref_type=heads#L639
	caerDeviceConfigSet(dvxplorer_handle, DVX_DVS_CHIP, DVX_DVS_CHIP_DTAG_CONTROL, DVX_DVS_CHIP_DTAG_CONTROL_STOP);
	caerDeviceConfigSet(dvxplorer_handle, DVX_DVS_CHIP, DVX_DVS_CHIP_FIXED_READ_TIME_ENABLE, false);
	caerDeviceConfigSet(dvxplorer_handle, DVX_DVS_CHIP, DVX_DVS_CHIP_TIMING_ED, 1);
	caerDeviceConfigSet(dvxplorer_handle, DVX_DVS_CHIP, DVX_DVS_CHIP_TIMING_NEXT_SEL, 5);
	caerDeviceConfigSet(dvxplorer_handle, DVX_DVS_CHIP, DVX_DVS_CHIP_DTAG_CONTROL, DVX_DVS_CHIP_DTAG_CONTROL_START);

	int udp_ev_counter = 0;
	while (!atomic_load_explicit(&globalShutdown, memory_order_relaxed)) {

		caerEventPacketContainer packetContainer = caerDeviceDataGet(dvxplorer_handle);
		if (packetContainer == NULL) {
			continue; // Skip if nothing there.
		}


		int32_t packetNum = caerEventPacketContainerGetEventPacketsNumber(packetContainer);
		// printf("\nGot event container with %d packets (allocated).\n", packetNum);



		for (int32_t i = 0; i < packetNum; i++) {
			caerEventPacketHeader packetHeader = caerEventPacketContainerGetEventPacket(packetContainer, i);
			if (packetHeader == NULL) {
				printf("Packet %d is empty (not present).\n", i);
				continue; // Skip if nothing there.
			}



			// Packet 0 is always the special events packet for DVS128, while packet is the polarity events packet.
			if (i == POLARITY_EVENT) {

				int32_t ev_nb = caerEventPacketHeaderGetEventNumber(packetHeader);
				// printf("Packet %d of type %d -> size is %d.\n", i, caerEventPacketHeaderGetEventType(packetHeader), ev_nb);

				caerPolarityEventPacket polarity = (caerPolarityEventPacket) packetHeader;

				// Get full timestamp and addresses of first event.

				for (int32_t j = 0; j < ev_nb; j++){
					caerPolarityEventConst current_event = caerPolarityEventPacketGetEventConst(polarity, j);

					int32_t ts = caerPolarityEventGetTimestamp(current_event);
					uint16_t x = caerPolarityEventGetX(current_event);
					uint16_t y = caerPolarityEventGetY(current_event);
					bool pol   = caerPolarityEventGetPolarity(current_event);

					// printf("First polarity event - ts: %d, x: %d, y: %d, pol: %d.\n", ts, x, y, pol);

					if(x < 280 && y < 180){
						eventArray[udp_ev_counter] = noTimestamp + (pol << pShift) + (y << yShift) + (x << xShift);
						// printf("...\n");	
						udp_ev_counter++;
						if (udp_ev_counter == NUM_EVENTS){
							sendto(sock, eventArray, NUM_EVENTS * sizeof(unsigned int), 0, (struct sockaddr *)&server, sizeof(server));
							memcpy(eventArray, empty, NUM_EVENTS * sizeof(unsigned int));
							udp_ev_counter = 0;
						}

					}
				}
			}
		}
		caerEventPacketContainerFree(packetContainer);

    }
	caerDeviceDataStop(dvxplorer_handle);

	caerDeviceClose(&dvxplorer_handle);

	printf("Shutdown successful.\n");

	return (EXIT_SUCCESS);


}