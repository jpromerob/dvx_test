#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <arpa/inet.h>

#define IP "172.16.222.30"
#define PORT 3330

#define NUM_EVENTS 8

void generateEvent(int cx, int cy, int r) {
    int numPoints = 360;
    int pShift = 15;
    int yShift = 0;
    int xShift = 16;
    unsigned int noTimestamp = 0x80000000;
    int sock;
    struct sockaddr_in server;
    

    unsigned int *eventArray = (unsigned int *)malloc(NUM_EVENTS * sizeof(unsigned int));
    if (eventArray == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

	for (int i = 0; i < NUM_EVENTS; i++) {
		eventArray[i] = 0;
	}

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(IP);

    int polarity = 1;
    int step = 0;
    int udp_ev_counter = 0;
    while (1) {
        double angle = 2 * M_PI * step / numPoints;
        int x = cx + r * cos(angle);
        int y = cy + r * sin(angle);
        step = (step + 1) % numPoints;
        eventArray[udp_ev_counter] = noTimestamp + (polarity << pShift) + (y << yShift) + (x << xShift);
        if (udp_ev_counter+1 < NUM_EVENTS){
            udp_ev_counter++;
        } else {
            udp_ev_counter = 0     ;     
			sendto(sock, eventArray, NUM_EVENTS * sizeof(unsigned int), 0, (struct sockaddr *)&server, sizeof(server));
            usleep(10);  // Sleep for 0.2 milliseconds (200 microseconds)
        }
        
        
    }

    close(sock);
}

int main() {
    int cx = 140;
    int cy = 90;
    int r = 20;

    generateEvent(cx, cy, r);

    return 0;
}