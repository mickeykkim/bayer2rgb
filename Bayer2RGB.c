#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bayer.h"

/**
 *
 * Attributes that can be passed by ImageMagick
 *
 *  %i  input image filename
 *  %o  output image filename
 *  %u  unique temporary filename
 *  %z  secondary unique temporary filename
 *  %#  input image signature
 *  %b  image file size
 *  %c  input image comment
 *  %g  image geometry
 *  %h  image rows (height)
 *  %k  input image number colors
 *  %m  input image format
 *  %p  page number
 *  %q  input image depth
 *  %s  scene number
 *  %w  image columns (width)
 *  %x  input image x resolution
 *  %y  input image y resolution
 *
 *  ./Bayer2RGB -i %i -o %o -w %x -h %y -b %q
 */



dc1394bayer_method_t
getMethod(char* m)
{
	if( strcmp(m, "NEAREST") == 0 )
		return DC1394_BAYER_METHOD_NEAREST;
	if( strcmp(m, "SIMPLE") == 0 )
		return DC1394_BAYER_METHOD_SIMPLE;
	if( strcmp(m, "BILINEAR") == 0 )
		return DC1394_BAYER_METHOD_BILINEAR;
	if( strcmp(m, "HQLINEAR") == 0 )
		return DC1394_BAYER_METHOD_HQLINEAR;
	if( strcmp(m, "DOWNSAMPLE") == 0 )
		return DC1394_BAYER_METHOD_DOWNSAMPLE;
	if( strcmp(m, "EDGESENSE") == 0 )
		return DC1394_BAYER_METHOD_EDGESENSE;
	if( strcmp(m, "VNG") == 0 )
		return DC1394_BAYER_METHOD_VNG;
	if( strcmp(m, "AHD") == 0 )
		return DC1394_BAYER_METHOD_AHD;

	printf("WARNING: Unrecognized method, defaulting to BILINEAR\n");
	return DC1394_BAYER_METHOD_BILINEAR;
}


dc1394color_filter_t
getFirstColor(char *f)
{
	if( strlen(f) < 2 )
		goto fc_unrec;
	if( strcmp(f, "RGGB") == 0 )
		return DC1394_COLOR_FILTER_RGGB;
	if( strcmp(f, "GBRG") == 0 )
		return DC1394_COLOR_FILTER_GBRG;
	if( strcmp(f, "GRBG") == 0 )
		return DC1394_COLOR_FILTER_GRBG;
	if( strcmp(f, "BGGR") == 0 )
		return DC1394_COLOR_FILTER_BGGR;
fc_unrec:
	printf("WARNING: Unrecognized first color, defaulting to RGGB\n");
	return DC1394_COLOR_FILTER_RGGB;
}

void
usage( char * name )
{
	printf("usage: %s\n", name);
	printf("   --input,-i     input file\n");
	printf("   --output,-o    output file\n");
	printf("   --width,-w     image width (pixels)\n");
	printf("   --height,-v    image height (pixels)\n");
	printf("   --bpp,-b       bits per pixel\n");
	printf("   --first,-f     first pixel color: RGGB, GBRG, GRBG, BGGR\n");
	printf("   --method,-m    interpolation method: NEAREST, SIMPLE, BILINEAR, HQLINEAR, DOWNSAMPLE, EDGESENSE, VNG, AHD\n");
	printf("   --help,-h      this helpful message.\n");
}

int
main( int argc, char ** argv )
{
    uint32_t ulInSize, ulOutSize, ulWidth, ulHeight, ulBpp;
    int first_color = DC1394_COLOR_FILTER_RGGB;
	int method = DC1394_BAYER_METHOD_BILINEAR;
    char *infile, *outfile;
    int input_fd = 0;
    int output_fd = 0;
    void * pbyBayer = NULL;
    void * pbyRGB = NULL;
    char c;
    int optidx = 0;

    struct option longopt[] = {
        {"input",1,NULL,'i'},
        {"output",1,NULL,'o'},
        {"width",1,NULL,'w'},
        {"height",1,NULL,'v'},
        {"help",0,NULL,'h'},
        {"bpp",1,NULL,'b'},
        {"first",1,NULL,'f'},
        {"method",1,NULL,'m'},
        {0,0,0,0}
    };

    while ((c=getopt_long(argc,argv,"i:o:w:v:b:f:m:h",longopt,&optidx)) != -1)
    {
        switch ( c )
        {
            case 'i':
                infile = strdup( optarg );
                break;
            case 'o':
                outfile = strdup( optarg );
                break;
            case 'w':
                ulWidth = strtol( optarg, NULL, 10 );
                break;
            case 'v':
                ulHeight = strtol( optarg, NULL, 10 );
                break;
            case 'b':
                ulBpp = strtol( optarg, NULL, 10 );
                break;
            case 'f':
                first_color = getFirstColor( optarg );
                break;
            case 'm':
				method = getMethod( optarg );
                break;
			case 'h':
				usage(argv[0]);
				return 0;
				break;
            default:
                printf("bad arg\n");
				usage(argv[0]);
                return 1;
        }
    }
    // arguments: infile outfile width height bpp first_color
    if( infile == NULL || outfile == NULL || ulBpp == 0 || ulWidth == 0 || ulHeight == 0 )
    {
        printf("Bad parameter\n");
		usage(argv[0]);
        return 1;
    }

    input_fd = open(infile, O_RDONLY);
    if(input_fd <= 0)
    {
        printf("Problem opening input: %s\n", infile);
        return 1;
    }

    output_fd = open(outfile, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH );
    if(output_fd <= 0)
    {
        printf("Problem opening output: %s\n", outfile);
        return 1;
    }

    ulInSize = lseek(input_fd, 0, SEEK_END );
    lseek(input_fd, 0, 0);

    //ulOutSize = ulWidth * ulHeight * ulBpp * 3;
    ulOutSize = ulWidth * ulHeight * (8 / ulBpp) * 3;
    ftruncate(output_fd, ulOutSize );

    pbyBayer = mmap(NULL, ulInSize, PROT_READ,  MAP_SHARED|MAP_POPULATE, input_fd, 0);
    if( pbyBayer == MAP_FAILED )
    {
        perror("Faild mmaping input");
        return 1;
    }
    pbyRGB = mmap(NULL, ulOutSize, PROT_READ | PROT_WRITE, MAP_SHARED|MAP_POPULATE, output_fd, 0);
    if( pbyRGB == MAP_FAILED )
    {
        perror("Faild mmaping output");
        return 1;
    }
    printf("%p -> %p\n", pbyBayer, pbyRGB);

    printf("%s: %s(%d) %s(%d) %d %d %d\n", argv[0], infile, ulInSize, outfile, ulOutSize, ulWidth, ulHeight, ulBpp );

    //memset(pbyRGB, 0xff, ulOutSize);//return 1;

	switch(ulBpp)
	{
		case 8:
			dc1394_bayer_decoding_8bit((uint8_t*)pbyBayer, (uint8_t*)pbyRGB, ulWidth, ulHeight, first_color, method);
			break;
		case 16:
		default:
			dc1394_bayer_decoding_16bit((uint16_t*)pbyBayer, (uint16_t*)pbyRGB, ulWidth, ulHeight, first_color, method, ulBpp);
			break;
	}

    munmap(pbyBayer,ulInSize);
    close(input_fd);

    msync(pbyRGB,ulOutSize,MS_INVALIDATE|MS_SYNC);
    munmap(pbyRGB,ulOutSize);
    close(output_fd);

    return 0;
}