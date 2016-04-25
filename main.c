#include "SDL/SDL.h"
#include "SDL/SDL_image.h"
#include "SDL/SDL_mixer.h"
#include "SDL/SDL_opengl.h"
#include <stdio.h>
#include <sys/time.h>
#include <math.h>

// JPGV video struct
typedef struct{
	uint32_t framerate;
	uint16_t audiotype;
	uint16_t bytepersample;
	uint16_t samplerate;
	uint16_t audiocodec;
	uint32_t tot_frame;
	uint32_t audiobuf_size;
}video_info;

// Some used variables (TODO: Place them in main to not use Heap)
SDL_Surface* frame = NULL;
struct timeval tick1, tick2;
uint64_t* vidbuf_offs;
FILE* jpgv;
FILE* audio_ptr;
video_info info;
char* buffer;
GLuint texture=NULL;
GLenum texture_format=NULL;
GLint nofcolors;
SDL_AudioSpec device;
uint32_t audio_len;
uint8_t* audio_pos;
Mix_Music* mix_chunk = NULL;

// Drawing function using openGL to rotate the frame
void drawFrame(){
	glClear( GL_COLOR_BUFFER_BIT );
	glRotatef( 270.0, 0.0, 0.0, 1.0 );
	glTranslatef(-240.0,0.0,0.0);
	glBindTexture( GL_TEXTURE_2D, texture );
	glBegin( GL_QUADS );
	glTexCoord2i( 0, 0 );
	glVertex3f( 0, 0, 0 );
	glTexCoord2i( 1, 0 );
	glVertex3f( 240, 0, 0 );
	glTexCoord2i( 1, 1 );
	glVertex3f( 240, 400, 0 );
	glTexCoord2i( 0, 1 );
	glVertex3f( 0, 400, 0 );
	glEnd();
	glLoadIdentity();
	SDL_GL_SwapBuffers();
}

// Audio callback for PCM16 audiocodec
void wav_callback(void *udata, Uint8 *stream, int len){
	if (audio_len == 0) return;
	len = ( len > audio_len ? audio_len : len );
	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}

// Video callback to update 2D texture with current frame
void updateFrame(){

	SDL_FreeSurface(frame);
	free(buffer);
	
	// Calculating current frame
	gettimeofday(&tick2, NULL);
	uint32_t current_frame = floor(((tick2.tv_sec - tick1.tv_sec) + ((float)(tick2.tv_usec - tick1.tv_usec) / 1000000.0f)) * info.framerate);
	
	// Loading frame
	uint32_t jpg_size = vidbuf_offs[current_frame+1]-vidbuf_offs[current_frame];
	buffer = (char*)malloc(jpg_size);
	fseek(jpgv, (info.tot_frame<<3)+vidbuf_offs[current_frame], SEEK_SET);
	fread(buffer, 1, jpg_size, jpgv);
	SDL_RWops* rw = SDL_RWFromMem(buffer,jpg_size);
	frame = IMG_Load_RW(rw, 1);
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexImage2D( GL_TEXTURE_2D, 0, nofcolors, frame->w, frame->h, 0, texture_format, GL_UNSIGNED_BYTE, frame->pixels );
	
}

int main(int argc, char* argv[]){
	
	// Opening JPGV video
	char* filename;
	if (argc < 2){
		printf("Usage: jpgv_player FILE.JPGV\n");
		return -1;
	}else filename = argv[1];
	jpgv = fopen(filename, "rb");
	
	// Checking magic
	char magic[5];
	fread(&magic, 1, 4, jpgv);
	if (strncmp(magic,"JPGV",4) != 0){
		printf("JPGV video is corrupted.");
		fclose(jpgv);
		return -1;
	}
	
	// Reading video info
	fread(&info.framerate, 1, 4, jpgv);
	fread(&info.audiotype, 1, 2, jpgv);
	fread(&info.bytepersample, 1, 2, jpgv);
	fread(&info.samplerate, 1, 2, jpgv);
	fread(&info.audiocodec, 1, 2, jpgv);
	fread(&info.tot_frame, 1, 4, jpgv);
	fread(&info.audiobuf_size, 1, 4, jpgv);
	
	// Initializing SDL and openGL stuffs
	uint8_t quit = 0;
	SDL_Event event;
	SDL_Surface* screen = NULL;
	SDL_Init( SDL_INIT_EVERYTHING );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	screen = SDL_SetVideoMode( 400, 240, 32, SDL_OPENGL );
	glClearColor( 0, 0, 0, 0 );
	glEnable( GL_TEXTURE_2D );
	glViewport( 0, 0, 400, 240 );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0, 400, 240, 0, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	SDL_WM_SetCaption("Minimalist JPGV Player", NULL);
	char* audiobuffer;
	
	if (info.audiocodec){
		
		// Allocating audiobuffer (TODO: A proper streaming function)
		audiobuffer = (char*)malloc(info.audiobuf_size);
		fread(audiobuffer, 1, info.audiobuf_size, jpgv);
		SDL_RWops* rw_audio = SDL_RWFromMem(audiobuffer,info.audiobuf_size);
		
		// Extracting missing video values from OGG container
		memcpy(&info.audiotype,&audiobuffer[39],1);
		memcpy(&info.samplerate,&audiobuffer[40],2);
		info.bytepersample = info.audiotype<<1;
		
		// Setting SDL audio device parameters
		Mix_OpenAudio(info.samplerate,AUDIO_S16LSB,info.audiotype,640);
		mix_chunk = Mix_LoadMUS_RW(rw_audio);
		if (mix_chunk == NULL) printf("ERROR: An error occurred while opening audio sector.\n%s\n",Mix_GetError());
		
	}else{
	
		// Allocating audiobuffer (TODO: A proper streaming function)
		audio_len = info.audiobuf_size;
		audiobuffer = (char*)malloc(info.audiobuf_size);
		fread(audiobuffer, 1, info.audiobuf_size, jpgv);
		
		// Setting SDL audio device parameters
		device.freq = info.samplerate;
		device.format = AUDIO_S16LSB;
		device.channels = info.audiotype;
		device.samples = 1024;
		device.callback = wav_callback;
		device.userdata = audiobuffer;
		audio_pos = audiobuffer;
		if (SDL_OpenAudio(&device, NULL) < 0) printf("ERROR: An error occurred while opening audio sector.\n%s\n", SDL_GetError());
	
	}
	
	// Writing Video Info on Screen
	printf("Video Info:\n");
	printf("Framerate: %lu\n", info.framerate);
	printf("Audiotype: %s\n", (info.audiotype>1) ? "Stereo" : "Mono");
	printf("Bytepersample: %u\n", info.bytepersample);
	printf("Samplerate: %u\n", info.samplerate);
	printf("Audiocodec: %s\n", info.audiocodec ? "Vorbis" : "PCM16");
	printf("Total Frames Number: %lu\n", info.tot_frame);
	//printf("Audiobuffer Size: %lu\n", info.audiobuf_size); DEBUG
	
	// Parsing video buffer offsets table
	vidbuf_offs = (uint64_t*)malloc(info.tot_frame<<3);
	uint32_t i;
	for (i=0;i<info.tot_frame;i++){
		fread(&vidbuf_offs[i], 1, 8, jpgv);	}
	
	// Loading first frame
	uint32_t jpg_size = vidbuf_offs[1]-vidbuf_offs[0];
	buffer = (char*)malloc(jpg_size);
	fseek(jpgv, (info.tot_frame<<3)+vidbuf_offs[0], SEEK_SET);
	fread(buffer, 1, jpg_size, jpgv);
	SDL_RWops* rw = SDL_RWFromMem(buffer,jpg_size);
	frame = IMG_Load_RW(rw, 1);
	
	// Generating 2D Texture for openGL
	nofcolors=frame->format->BytesPerPixel;
	if(nofcolors==4){
		if(frame->format->Rmask==0x000000ff) texture_format=GL_RGBA;
		else texture_format=GL_BGRA;
	}else if(nofcolors==3){
		if(frame->format->Rmask==0x000000ff) texture_format=GL_RGB;
		else texture_format=GL_BGR;
	}
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexImage2D( GL_TEXTURE_2D, 0, nofcolors, frame->w, frame->h, 0, texture_format, GL_UNSIGNED_BYTE, frame->pixels );
	
	// Starting audio playback
	if (mix_chunk == NULL) SDL_PauseAudio(0);
	else Mix_PlayMusic(mix_chunk,0); 
	
	// Starting timer
	gettimeofday(&tick1, NULL);
	
	// Main loop
	while(!quit){
		while( SDL_PollEvent( &event ) ) {
			if( event.type == SDL_QUIT ) {
				quit = 1;
			} 
		}
		updateFrame();
		drawFrame();
	}
	
	// Freeing everything
	SDL_CloseAudio();
	free(audiobuffer);
	SDL_FreeSurface( frame );
	free(buffer);
	SDL_Quit(); 
	
	return 0;
}