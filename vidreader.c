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
#include <ncurses.h>

#define WINDOW_WIDTH 280
#define WINDOW_HEIGHT 181
#define K_SZ 29

#define NB_COLS 280
#define NB_ROWS 181
#define NB_FRAMES 5000

#define BUFFER_SIZE 1024 * 256
#define PORT_UDP_RAW 3330
#define PORT_UDP_CNN 3331

int video_raw_mat[NB_FRAMES][NB_COLS][NB_ROWS];
int video_cnn_mat[NB_FRAMES][NB_COLS][NB_ROWS];
int emptyMatrix[NB_COLS][NB_ROWS];
int scale = 1;
int nb_1ms_frames_per_frame = 1;
int frame_count = 0;
int frame_step = 1;
bool show_target = false;

pthread_mutex_t cur_frame_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to calculate Euclidean distance between two points
double get_distance(double x1, double y1, double x2, double y2) {
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

// Function to load video_raw_mat data from a file
void loadVideoData(const char *filename, int video_mat[NB_FRAMES][NB_COLS][NB_ROWS]) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Failed to open file for reading");
        exit(1);
    }

    fread(video_mat, sizeof(int), NB_FRAMES * NB_COLS * NB_ROWS, file);
    fclose(file);
}

// Function to render the entire matrix as a texture
void* renderMatrix(void* arg) {

    
    const char *raw_vid_fn = "raw_video.dat";
    loadVideoData(raw_vid_fn, video_raw_mat);
    const char *cnn_vid_fn = "cnn_video.dat";
    loadVideoData(cnn_vid_fn, video_cnn_mat);

    int cur_frame = 0;

    int black = 0xFF000000;
    int green = 0xFF006600;
    int blue = 0xFF0000FF;
    int red = 0xFFFF0000;
    int yellow = 0xFFFFFF00;

    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow("Air Hockey", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, scale*WINDOW_WIDTH, scale*WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* matrixTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NB_COLS, NB_ROWS);

    while (1) {
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
        
        pthread_mutex_lock(&cur_frame_mutex);
        cur_frame = frame_count;
        pthread_mutex_unlock(&cur_frame_mutex);

        int sum_idx_x = 0;
        int sum_idx_y = 0;
        int count_ones = 0;
        int avg_idx_x = 0;
        int avg_idx_y = 0;
        for (int y = 0; y < NB_ROWS; y++) {
            for (int x = 0; x < NB_COLS; x++) {
                if(video_cnn_mat[cur_frame][x][y]>0){
                    sum_idx_x+=x;
                    sum_idx_y+=y;
                    count_ones++;
                }
            }
        }
        
        if(count_ones > 0) {
            avg_idx_x = sum_idx_x/count_ones;
            avg_idx_y = sum_idx_y/count_ones;
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
                for(int k=0; k < nb_1ms_frames_per_frame; k++){
                    if (video_raw_mat[cur_frame+k][x][y] == 1) {
                        // Set color to green (0xFF00FF00) if the matrix value is 1
                        color = green;
                    } 
                    if (video_cnn_mat[cur_frame+k][x][y] == 1) {
                        // Set color to green (0xFF00FF00) if the matrix value is 1
                        color = yellow;
                    } 
                }
                if (show_target){
                    if (get_distance(x, y, avg_idx_x, avg_idx_y)<=3){
                        color = red;
                    }
                }
                
                ((Uint32*)pixels)[index] = color;
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


// Function to render the entire matrix as a texture
void* handleCommand(void* arg) {
    
    initscr();           // Initialize ncurses
    keypad(stdscr, TRUE); // Enable arrow key support

    int ch;
    while (1) {
        ch = getch(); // Get a character from the keyboard

        // Check which arrow key was pressed and print a corresponding number
        switch (ch) {
            case KEY_UP:
                frame_step++;
                printw("++ (frame step #%d)      \r", frame_step);
                break;
            case KEY_DOWN:
                if(frame_step>1){
                    frame_step--;
                }
                printw("-- (frame step #%d)      \r", frame_step);
                break;
            case KEY_LEFT:
                if(frame_count>=frame_step){
                    frame_count -= frame_step;
                } else {
                    frame_count = 0;
                }
                printw("<< (frame #%d)      \r", frame_count);
                break;
            case KEY_RIGHT:
                if(frame_count<NB_FRAMES-frame_step){
                    frame_count+=frame_step;
                } else {
                    frame_count = NB_FRAMES;
                }
                printw(">> (frame #%d)      \r", frame_count);
                break;
            case 27: // 27 is the ASCII code for the Escape key (to exit the loop)
                endwin(); // Clean up ncurses before exiting
                return 0;
            default:
                break;
        }
        refresh(); // Refresh the screen to show the printed text
    }

    endwin(); // Clean up ncurses before exiting
    return 0;

}

int main(int argc, char* argv[]) {

    if (argc != 4) {
        printf("Usage: %s <scale> <show-target> <nb_1ms_frames_per_frame>\n", argv[0]);
        return 1;
    }

    // Convert the command-line arguments (strings) to integers
    scale = atoi(argv[1]);
    show_target = (bool)atoi(argv[2]);
    nb_1ms_frames_per_frame = atoi(argv[3]);


    pthread_t renderThread, commandThread;

    // Create threads
    pthread_create(&renderThread, NULL, renderMatrix, NULL);
    pthread_create(&commandThread, NULL, handleCommand, NULL);

    // Wait for threads to finish (this will never happen in this example)
    pthread_join(renderThread, NULL);
    pthread_join(commandThread, NULL);

    // Clean up mutex
    pthread_mutex_destroy(&cur_frame_mutex);

    return 0;
}
