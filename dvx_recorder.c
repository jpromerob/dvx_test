#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <SDL.h>
#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

#define XYP_TIME 0.000010
#define COLOR_HIGH 1

#define WINDOW_WIDTH 280
#define WINDOW_HEIGHT 181
#define K_SZ 29

#define NB_COLS 280
#define NB_ROWS 181
#define NB_FRAMES 5000

#define BUFFER_SIZE 1024
#define PORT_UDP_RAW 3330
#define PORT_UDP_CNN 3331
#define PORT_UDP_XYP 3334

int emptyMatrix[NB_COLS][NB_ROWS];
int input_raw_mat[NB_COLS][NB_ROWS]; // where incoming raw data is stored
int visual_raw_mat[NB_COLS][NB_ROWS]; // matrix used by render (raw)
int shared_raw_mat[NB_COLS][NB_ROWS]; // matrix shared between udp receiver (raw) and render|saver
int input_cnn_mat[NB_COLS][NB_ROWS]; // where incoming scnn data is stored
int visual_cnn_mat[NB_COLS][NB_ROWS]; // matrix used by render (scnn)
int shared_cnn_mat[NB_COLS][NB_ROWS]; // matrix shared between udp receiver (scnn) and render|saver

int video_raw_mat[NB_FRAMES][NB_COLS][NB_ROWS];
int video_cnn_mat[NB_FRAMES][NB_COLS][NB_ROWS];

float acc_time = 0.001000;
int x_px = 0;
int y_px = 0;
int x_cm = 0;
int y_cm = 0;
int scale = 1;
bool is_live = false;
bool is_neural = false;
bool is_the_end = false;


pthread_mutex_t end_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t xyp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t raw_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cnn_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to calculate Euclidean distance between two points
double get_distance(double x1, double y1, double x2, double y2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}


// Function to save video_raw_mat data to a file
void saveVideoData(const char *filename, int video_mat[NB_FRAMES][NB_COLS][NB_ROWS]) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Failed to open file for writing");
        exit(1);
    }

    fwrite(video_mat, sizeof(int), NB_FRAMES * NB_COLS * NB_ROWS, file);
    fclose(file);
}

void* updateRaw(void* arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    } else {
        printf("Socket for RAW correctly created\n");
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT_UDP_RAW); // Use the same port as in Python

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Listening for RAW data...\n");

    struct timespec start_time, current_time;
    bool local_end = false;

    while (!local_end) {

        
        pthread_mutex_lock(&end_mutex);  
        local_end = is_the_end;
        pthread_mutex_unlock(&end_mutex);  
        double elapsed_time;
    
        // Get the current time
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int recv_counter = 0;
        // Perform the operation repeatedly until 1ms (0.001 seconds) has elapsed
        do {
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);

            if (recv_len < 0) {
                perror("recvfrom");
                exit(1);
            } else {
                recv_counter++;
            }

            // Assuming the received data is packed as little-endian 32-bit integers
            for (int i = 0; i < recv_len; i += 4) {
                unsigned int packed_data;
                memcpy(&packed_data, &buffer[i], 4);

                // Extract x and y values
                int x = (packed_data >> 16) & 0x00003FFF;
                int y = (packed_data >> 0) & 0x00003FFF;

                input_raw_mat[x][y] += 1;
            }
            
            // Get the current time
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            
            // Calculate the elapsed time in seconds
            elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
        } while (elapsed_time < acc_time/1000); // Repeat until 1ms has elapsed
        
        // Acquire mutex lock | update data | release mutex lock
        pthread_mutex_lock(&raw_mutex);        
        memcpy(shared_raw_mat, input_raw_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&raw_mutex);

        memcpy(input_raw_mat, emptyMatrix, sizeof(int) * NB_COLS * NB_ROWS);

    }

    close(sockfd);

    return NULL;
}

void* updateXyp(void* arg){

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    } else {
        printf("Socket for XYP correctly created\n");
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT_UDP_XYP); // Use the same port as in Python

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Listening for XYP data...\n");

    struct timespec start_time, current_time;

    bool local_end = false;

    while (!local_end) {

        
        pthread_mutex_lock(&end_mutex);  
        local_end = is_the_end;
        pthread_mutex_unlock(&end_mutex);  

        double elapsed_time;
    
        // Get the current time
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int recv_counter = 0;
        // Perform the operation repeatedly until 1ms (0.001 seconds) has elapsed
        do {
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);

            if (recv_len < 0) {
                perror("recvfrom");
                exit(1);
            } else {
                recv_counter++;
            }

            int x = 0;
            int y = 0;
            int x_counter = 0;
            int y_counter = 0;
            // Assuming the received data is packed as little-endian 32-bit integers
            for (int i = 0; i < recv_len; i += 4) {
                unsigned int packed_data;
                memcpy(&packed_data, &buffer[i], 4);

                // Extract x and y values
                int xy_val = (packed_data >> 16) & 0x00003FFF;
                if(xy_val < WINDOW_WIDTH-K_SZ+1){
                    x += xy_val+K_SZ/2;
                    x_counter++;
                } else {
                    y += xy_val-(WINDOW_WIDTH-K_SZ+1)+K_SZ/2;
                    y_counter++;
                }

            }
            pthread_mutex_lock(&xyp_mutex);  
            if(x_counter>0 && y_counter>0){
                x_px = x/x_counter;
                y_px = y/y_counter;
            }
            // printf("(%d,%d)\n", x_px, y_px);
            pthread_mutex_unlock(&xyp_mutex);  
            
            
            // Get the current time
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            
            // Calculate the elapsed time in seconds
            elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
        } while (elapsed_time < XYP_TIME); // Repeat until 1ms has elapsed
        

    }

    close(sockfd);

    return NULL;

}

void* updateCnn(void* arg) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    unsigned char buffer[BUFFER_SIZE];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    } else {
        printf("Socket for CNN correctly created\n");
    }

    // Initialize server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT_UDP_CNN); // Use the same port as in Python

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Listening for CNN data...\n");

    struct timespec start_time, current_time;
    bool local_end = false;

    while (!local_end) {

        
        pthread_mutex_lock(&end_mutex);  
        local_end = is_the_end;
        pthread_mutex_unlock(&end_mutex);  
    
        double elapsed_time;
    
        // Get the current time
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        
        int recv_counter = 0;
        // Perform the operation repeatedly until 1ms (0.001 seconds) has elapsed
        do {
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&client_addr, &client_addr_len);

            if (recv_len < 0) {
                perror("recvfrom");
                exit(1);
            } else {
                recv_counter++;
            }

            // Assuming the received data is packed as little-endian 32-bit integers
            for (int i = 0; i < recv_len; i += 4) {
                unsigned int packed_data;
                memcpy(&packed_data, &buffer[i], 4);

                // Extract x and y values
                int x = (K_SZ/2)+ ((packed_data >> 16) & 0x00003FFF);
                int y = (K_SZ/2)+ ((packed_data >> 0) & 0x00003FFF);

                input_cnn_mat[x][y] += 1;
            }
            
            // Get the current time
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            
            // Calculate the elapsed time in seconds
            elapsed_time = (current_time.tv_sec - start_time.tv_sec) +
                        (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
            
        } while (elapsed_time < acc_time/1000); // Repeat until 1ms has elapsed
        
        // Acquire mutex lock | update data | release mutex lock
        pthread_mutex_lock(&cnn_mutex);        
        memcpy(shared_cnn_mat, input_cnn_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&cnn_mutex);

        memcpy(input_cnn_mat, emptyMatrix, sizeof(int) * NB_COLS * NB_ROWS);

    }

    close(sockfd);

    return NULL;
}
// Function to render the entire matrix as a texture
void* renderMatrix(void* arg) {

    if(!is_live){
        return NULL;
    }

    int base = 0xFF000000;
    int black = 0x00000000;
    int green = 0x00006600;
    int blue = 0x000000FF;
    int red = 0x00FF0000;
    int yellow = 0x00FFFF00;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow("Air Hockey", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, scale*WINDOW_WIDTH, scale*WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* matrixTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NB_COLS, NB_ROWS);

    int local_x_px = 0;
    int local_y_px = 0;

    bool local_end = false;

    while (!local_end) {

        
        pthread_mutex_lock(&end_mutex);  
        local_end = is_the_end;
        pthread_mutex_unlock(&end_mutex);  
        
        SDL_Event e;
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                // Cleanup and quit SDL
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                exit(0);
            }
        }

        // Acquire mutex lock | get data | release mutex lock
        pthread_mutex_lock(&raw_mutex);
        memcpy(visual_raw_mat, shared_raw_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&raw_mutex);

        pthread_mutex_lock(&cnn_mutex);
        memcpy(visual_cnn_mat, shared_cnn_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&cnn_mutex);



        if(is_neural){
            pthread_mutex_lock(&xyp_mutex);  
            local_x_px = x_px;
            local_y_px = y_px;
            pthread_mutex_unlock(&xyp_mutex);  
        } else {

            int sum_idx_x = 0;
            int sum_idx_y = 0;
            int count_ones = 0;
            for (int y = 0; y < NB_ROWS; y++) {
                for (int x = 0; x < NB_COLS; x++) {
                    if(visual_cnn_mat[x][y]>0 && (x!=39 && y!= 166)){
                        sum_idx_x+=x;
                        sum_idx_y+=y;
                        count_ones++;
                    }
                }
            }
            
            if(count_ones > 0) {

                pthread_mutex_lock(&xyp_mutex);  
                x_px = sum_idx_x/count_ones;
                y_px = sum_idx_y/count_ones;
                local_x_px = x_px;
                local_y_px = y_px;
                pthread_mutex_unlock(&xyp_mutex);  
            }

        }
        



        // Lock the texture for writing
        void* pixels;
        int pitch;
        SDL_LockTexture(matrixTexture, NULL, &pixels, &pitch);

        int color = black;

        // Update the texture pixel data
        for (int y = 0; y < NB_ROWS; y++) {
            for (int x = 0; x < NB_COLS; x++) {
                int index = y * NB_COLS + x;
                color = black;
                if (visual_raw_mat[x][y] >= 1) {
                    // Set color to green (0xFF00FF00) if the matrix value is 1
                    color = green*(visual_raw_mat[x][y]/COLOR_HIGH);
                } 
                if (get_distance(x, y, local_x_px, local_y_px)<=3){
                    color = red;
                }
                if (visual_cnn_mat[x][y] >= 1) {
                    // Set color to green (0xFF00FF00) if the matrix value is 1
                    color = yellow*(visual_cnn_mat[x][y]/COLOR_HIGH);
                } 
                ((Uint32*)pixels)[index] = color + base;
            }
        }

        // Unlock the texture
        SDL_UnlockTexture(matrixTexture);

        // Clear the renderer
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Render the texture
        SDL_RenderCopy(renderer, matrixTexture, NULL, NULL);

        // Update the screen
        SDL_RenderPresent(renderer);

        // Delay to limit the frame rate
        SDL_Delay(16); // Adjust the delay as needed for your desired frame rate
    }
}

void* saveVideo(void* arg) {

    if(is_live){
        return NULL;
    }

    int frame_count = 0;
    while(1){
        pthread_mutex_lock(&raw_mutex);
        memcpy(visual_raw_mat, shared_raw_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&raw_mutex);
        
        pthread_mutex_lock(&cnn_mutex);
        memcpy(visual_cnn_mat, shared_cnn_mat, sizeof(int) * NB_COLS * NB_ROWS);
        pthread_mutex_unlock(&cnn_mutex);

        memcpy(video_raw_mat[frame_count], visual_raw_mat, sizeof(visual_raw_mat));
        memcpy(video_cnn_mat[frame_count], visual_cnn_mat, sizeof(visual_cnn_mat));

        usleep(1000*acc_time);
        if(frame_count%100==0){
            printf("%d frames\n", frame_count);
        }
        frame_count++;
        
        if(frame_count==NB_FRAMES/acc_time){
            printf("\nVideo Saved!\n");
            break;
        }
    }
    printf("End of Recording ...\n");   
    const char *raw_vid_fn = "raw_video.dat";
    saveVideoData(raw_vid_fn, video_raw_mat);
    const char *cnn_vid_fn = "cnn_video.dat";
    saveVideoData(cnn_vid_fn, video_cnn_mat);
    printf("File saved ...\n");   

    pthread_mutex_lock(&end_mutex);  
    is_the_end = true;
    pthread_mutex_unlock(&end_mutex);  

}


int main(int argc, char *argv[]) {

    if (argc != 5) {
        printf("Usage: %s <live|video> <neural|algebraic> <scale> <acc_time_ms>\n", argv[0]);
        return 1;
    }

    char *operation = argv[1];
    char *estimation = argv[2];
    scale = atoi(argv[3]);
    acc_time = (atof(argv[4]));


    if (strcmp(operation, "live") == 0) {
        is_live = true;
        printf("Live rendering with acc time of %f [ms] ...\n", acc_time);
    } else if (strcmp(operation, "video") == 0) {
        is_live = false;
        printf("Video recording with acc time of %f [ms] ...\n", acc_time);
    } else {
        printf("Unknown operation mode\n");
        return 0;
    }

    if (strcmp(estimation, "neural") == 0) {
        is_neural = true;
        printf("Using Neural Estimation\n");
    } else if (strcmp(estimation, "algebraic") == 0) {
        is_neural = false;
        printf("Using Algebraic Estimation\n");
    } else {
        printf("Unknown estimation mode\n");
        return 0;
    }


    memset(input_raw_mat, 0, sizeof(int) * NB_COLS * NB_ROWS);
    memset(shared_raw_mat, 0, sizeof(int) * NB_COLS * NB_ROWS);
    memset(emptyMatrix, 0, sizeof(int) * NB_COLS * NB_ROWS);

    pthread_t updateRawThread, updateCnnThread, updateXypThread;
    pthread_t renderThread, videoThread;

    // Create threads
    pthread_create(&updateRawThread, NULL, updateRaw, NULL);
    pthread_create(&updateCnnThread, NULL, updateCnn, NULL);
    pthread_create(&updateXypThread, NULL, updateXyp, NULL);
    pthread_create(&renderThread, NULL, renderMatrix, NULL);
    pthread_create(&videoThread, NULL, saveVideo, NULL);

    // Wait for threads to finish (this will never happen in this example)
    pthread_join(updateRawThread, NULL);
    pthread_join(updateCnnThread, NULL);
    pthread_join(updateXypThread, NULL);
    pthread_join(renderThread, NULL);
    pthread_join(videoThread, NULL);

    // Clean up mutex
    pthread_mutex_destroy(&xyp_mutex);
    pthread_mutex_destroy(&raw_mutex);
    pthread_mutex_destroy(&cnn_mutex);

    return 0;
}
