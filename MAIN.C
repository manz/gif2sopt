/***************************************************************************/
/* Includes                                                                */
/***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>




/***************************************************************************/
/* Externals                                                               */
/***************************************************************************/

extern short decoder (short bytesPerLine);




/***************************************************************************/
/* Variables                                                               */
/***************************************************************************/

/* command line options */
int screenRequired;
int coloursRequired;
int tilesRequired;
int flipOptimise;

/* GIF file pointer */
FILE* pictureFilePtr;

/* SNES format data file pointer */
FILE* dataFilePtr;

/* pixel image of display */
unsigned char* displayImage;
unsigned char* h_flippedData;
unsigned char* v_flippedData;
unsigned char* hv_flippedData;

/*  SNES format data */
unsigned int mapData[1024];
unsigned int optimisedMapData[1024];
struct
{
    unsigned char hFlip;
    unsigned char vFlip;
}
optimisedFlipData[1024];
unsigned char* tileData;

/* colour palette values */
unsigned char* red;
unsigned char* green;
unsigned char* blue;
unsigned int numColours;

/* number of bytes in each tile - 16 for 4 colour, 32 for 16 colour */
unsigned long bytesPerChar;

/* current horizontal scan line pointer into display image */
unsigned int displayLine;

/* picture details */
unsigned int imageWidth;
unsigned int imageHeight;
unsigned int leftOffset;
unsigned int topOffset;
int bad_code_count;

/* filenames */
char gifFilename[256];
char filename[256];




/***************************************************************************/
/* Constants                                                               */
/***************************************************************************/

/* number of pixels in a scan line */
#define NUM_X_PIXELS (256)

/* number of scan lines in display */
#define NUM_SCAN_LINES (256)

/* number of bytes for display image (@1 byte per pixel) */
#define DISPLAY_BYTES (65536)

/* SNES format data sizes */
#define TILE_DATA_BYTES (65536)

/* number of character rows on display */
#define NUM_ROWS (32)

/* number of characers in a row */
#define NUM_COLS (32)




/***************************************************************************/
/* name: setExtension                                                      */
/* desc: This function changes the extension of the specified filename.    */
/***************************************************************************/

void setExtension (char* filename,
                   char* extension,
                   int replace)
{
    int index = 0;
    int e_index = 0;

    while ((filename[index] != '\0') && (filename[index] != '.'))
        index++;

    if ((filename[index] == '\0') ||
        ((filename[index] == '.') && replace))
    {
        while (extension[e_index] != '\0')
            filename[index++] = extension[e_index++];

        filename[index] = '\0';
    }
}




/***************************************************************************/
/* name: get_byte                                                          */
/* desc: This function simply reads a single byte from the GIF file.       */
/***************************************************************************/

int get_byte (void)
{
    unsigned char byte;

    /* read byte */
    if (fread (&byte, 1, 1, pictureFilePtr) == 1)
        return (int)byte;
    else
        return 0;
}




/***************************************************************************/
/* name: get_word                                                          */
/* desc: This function simply reads 2 bytes from the GIF file and returns  */
/*       these as a 16-bit word value.                                     */
/***************************************************************************/

unsigned int get_word (void)
{
    unsigned char loByte;
    unsigned char hiByte;

    /* get low byte */
    loByte = get_byte ();

    /* get high byte */
    hiByte = get_byte ();

    /* return as a 16-bit word */
    return 256*hiByte+loByte;
}




/***************************************************************************/
/* name: readHeaderInformation                                             */
/* desc: This function reads all the GIF header information. It will check */
/*       for the GIF tag, read all the colour info and read all the        */
/*       dimensions of the GIF object.                                     */
/***************************************************************************/

int readHeaderInformation (void)
{
    unsigned char tag[6];
    int infoByte;

    /* get GIF tag */
    fread (tag, 6, 1, pictureFilePtr);

    /* check for GIF tag */
    if (strncmp ((const char*)tag, "GIF87a", 6))
    {
        printf ("ERROR : not a GIF file\n");
        return 0;
    }

    /* don't need... */
    get_word ();
    get_word ();

    /* get and decode information byte */
    infoByte = get_byte ();
    numColours = 0x01 << ((infoByte & 0x07) + 1);

    /* ensure that this image has 16 or 256 colours */
    if ((numColours != 16) && (numColours != 256))
    {
        printf ("ERROR : this is not a 16 or 256 colour image\n");
        return 0;
    }

    /* determine number of bytes for each character */
    bytesPerChar = (numColours==16)?32:64;

    /* don't need... */
    get_byte ();
    get_byte ();

    /* if colour palette info */
    if (infoByte & 0x80)
    {
        int index;

        /* allocate memory for holding colours */
        red = (unsigned char*)malloc (numColours);
        green = (unsigned char*)malloc (numColours);
        blue = (unsigned char*)malloc (numColours);

        /* loop over all colours */
        for (index = 0; index < numColours; index++)
        {
            /* get RGB intensities */
            red[index] = get_byte ();
            green[index] = get_byte ();
            blue[index] = get_byte ();
        }
    }

    /* don't need */
    get_byte ();

    /* get image dimensions */
    leftOffset = get_word ();
    topOffset = get_word ();
    imageWidth = get_word ();
    imageHeight = get_word ();

    /* don't need */
    get_byte ();

    /* return success */
    return 1;
}




/***************************************************************************/
/* name: out_line                                                          */
/* desc: This function is called by the GIF decoder each time it has       */
/*       decoded a line of pixels. This function simply stores the pixel   */
/*       data and on return indicates whether it expects to receive any    */
/*       more scan lines from the decoder.                                 */
/***************************************************************************/

int out_line (unsigned char pixels[],
              int length)
{
    /* start at left display offset of GIF object */
    int index = leftOffset;

    /* loop over all pixels, but not going off the edge of the display */
    while ((index < length) && (index < NUM_X_PIXELS))
    {
        /* evaluate offset into display image */
        unsigned long offset = (displayLine<<8) + index;

        /* set pixel in display */
        if (offset < DISPLAY_BYTES)
            displayImage[offset] = pixels[index];

        index++;
    }

    displayLine++;

    /* if not reached last scan line of display */
    if ((displayLine < imageHeight) && (displayLine < NUM_SCAN_LINES))
        /* then accept more scan lines */
        return 1;
    else
        /* otherwise no more thankyou */
        return -1;
}




/***************************************************************************/
/* name: getBitPlane                                                       */
/* desc: This function evaluates the data for the specified bit plane, for */
/*       the specified scan line of the tile at (col, row).                */
/***************************************************************************/

unsigned char getBitPlane (unsigned int col,
                           unsigned int row,
                           unsigned int scanLine,
			               unsigned int plane)
{
    int index;
    unsigned char bitPlane = 0;
    unsigned char* displayPtr = &(displayImage[(((row<<3)+scanLine)<<8)+
                                  (col<<3)]);
    /* loop over 8 pixels */
    for (index = 0; index < 8; index++)
        /* if the bit in this plane is set */
        if (displayPtr[index] & (0x01 << plane))
            /* then set it in our character's bit plane */
            bitPlane |= (0x80 >> index);

    return bitPlane;
}




/***************************************************************************/
/* name: hFlipCharacter                                                    */
/* desc: This horizontally flips the specified character data.             */
/***************************************************************************/

void hFlipCharacter (unsigned char* characterData)
{
    unsigned int topPtr = 0;
    unsigned int bottomPtr = 14;
    unsigned int offs;
    unsigned int maxOffset;
    unsigned int scanLine;

    if (numColours == 16)
        maxOffset = 2;
    else
        maxOffset = 4;

    for (scanLine = 0; scanLine < 4; scanLine++)
    {
        for (offs = 0; offs < maxOffset; offs++)
        {
            unsigned char temp = characterData[16*offs+topPtr];
            characterData[16*offs+topPtr] = characterData[16*offs+bottomPtr];
            characterData[16*offs+bottomPtr] = temp;

            temp = characterData[16*offs+topPtr+1];
            characterData[16*offs+topPtr+1] = characterData[16*offs+bottomPtr+1];
            characterData[16*offs+bottomPtr+1] = temp;
        }

        topPtr += 2;
        bottomPtr -= 2;
    }
}




/***************************************************************************/
/* name: vFlipCharacter                                                    */
/* desc: This vertically flips the specified character data.               */
/***************************************************************************/

void vFlipCharacter (unsigned char* characterData)
{
    unsigned int index;
    unsigned int maxIndex;
    unsigned int bit;

    unsigned char masks[8] =
    {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };

    for (index = 0; index < bytesPerChar; index++)
    {
        unsigned char data = 0;

        for (bit = 0; bit < 8; bit++)
            if (characterData[index] & masks[bit])
                data |= masks[7-bit];

        characterData[index] = data;
    }
}




/***************************************************************************/
/* name: generateSNESData                                                  */
/* desc: Set up mapData and generate tileData.                             */
/***************************************************************************/

void generateSNESData (void)
{
    unsigned long index;
    unsigned int row;
    unsigned int totalPasses;
    unsigned int loop;
    unsigned int character;

    /* loop over all character codes on screen */
    for (index = 0; index < 1024; index++)
    {
        mapData[index] = index;
        optimisedFlipData[index].hFlip = 0;
        optimisedFlipData[index].vFlip = 0;
    }

    /* reset index into tileData */
    index = 0;

    /* evaluate the total number of (2 colour plane) passes
       that needs to made over each character */
    if (numColours == 16)
        totalPasses = 2;
    else
        totalPasses = 4;

    /* loop over every row of characters */
    for (row = 0; row < NUM_ROWS; row++)
    {
        int col;

        /* loop over every column in this row */
        for (col = 0; col < NUM_COLS; col++)
        {
            int pass;

            /* make the appropriate number of passes over each
               character */
            for (pass = 0; pass < totalPasses; pass++)
            {
                int scanLine;

                /* for each scan line in character */
                for (scanLine = 0; scanLine < 8; scanLine++)
                {
                    int plane;

                    /* loop over 2 bit planes */
                    for (plane = 0; plane < 2; plane++)
                    {
                        unsigned char data;

                        /* get the data for this bit plane */
                        data = getBitPlane (col, row, scanLine,
                                            (pass<<1)+plane);

                        /* write it */
                        tileData[index++] = data;
                    }
                }
            }
        }
    }

    /* copy "normal" data to h/v/h&v flipped arrays */
    for (loop = 0; loop < 4; loop++)
    {
        unsigned int offset = loop*(TILE_DATA_BYTES/4);

        memcpy (&(h_flippedData[offset]), &(tileData[offset]), (TILE_DATA_BYTES/4));
        memcpy (&(v_flippedData[offset]), &(tileData[offset]), (TILE_DATA_BYTES/4));
        memcpy (&(hv_flippedData[offset]), &(tileData[offset]), (TILE_DATA_BYTES/4));
     }

    /* flip the h/v/h&v arrays */
    for (character = 0; character < 1024; character++)
    {
        unsigned long offset = bytesPerChar * (unsigned long)character;

        hFlipCharacter (&(h_flippedData[offset]));
        vFlipCharacter (&(v_flippedData[offset]));
        hFlipCharacter (&(hv_flippedData[offset]));
        vFlipCharacter (&(hv_flippedData[offset]));
     }
}




/***************************************************************************/
/* name: sameCharacters                                                    */
/* desc: Determine whether the tileData for the two characters are the     */
/*       same.                                                             */
/***************************************************************************/

int sameCharacters (unsigned int char1,
                    unsigned int char2,
                    unsigned char* hFlip,
                    unsigned char* vFlip)
{
    unsigned long char1index;
    unsigned long char2index;

    /* check for non flipped */
    char1index = bytesPerChar * char1;
    char2index = bytesPerChar * char2;

    /* check for straight duplication */
    if (!memcmp (&(tileData[char1index]),
                 &(tileData[char2index]),
                 bytesPerChar))
    {
        *hFlip = 0; *vFlip = 0; return 1;
    }

    /* if doing h/v flip optimisation */
    if (flipOptimise)
    {
        /* check for h flipped */
        if (!memcmp (&(tileData[char1index]),
                     &(h_flippedData[char2index]),
                     bytesPerChar))
        {
            *hFlip = 1; *vFlip = 0; return 1;
        }

        /* check for v flipped */
        if (!memcmp (&(tileData[char1index]),
                     &(v_flippedData[char2index]),
                     bytesPerChar))
        {
            *hFlip = 0; *vFlip = 1; return 1;
        }

        /* check for h & v flipped */
        if (!memcmp (&(tileData[char1index]),
                     &(hv_flippedData[char2index]),
                     bytesPerChar))
        {
            *hFlip = 1; *vFlip = 1; return 1;
        }
    }

    return 0;
}




/***************************************************************************/
/* name: optimiseMap                                                       */
/* desc: Replace identical characters.                                     */
/***************************************************************************/

void optimiseMap (void)
{
    int index;
    int nextTileNumber;
    int optimisedCharacters = 0;
    unsigned char hFlip;
    unsigned char vFlip;

    /* loop over all but last map characters */
    for (index = 0; index < 1023; index++)
        /* if this character hasn't been optimised already */
        if (mapData[index] == index)
        {
            int index2;

            /* loop over all other characters */
            for (index2 = index+1; index2 < 1024; index2++)
                /* if this character hasn't been optimised already */
                if (mapData[index2] == index2)
                    /* if the characters are the same */
                    if (sameCharacters (index, index2, &hFlip, &vFlip))
                    {
                        /* optimise character */
                        mapData[index2] = index;
                        optimisedFlipData[index2].hFlip = hFlip;
                        optimisedFlipData[index2].vFlip = vFlip;
                        optimisedCharacters++;
                    }
        }

    printf ("\n%d tiles have been optimised\n", optimisedCharacters);

    nextTileNumber = 0;

    for (index = 0; index < 1024; index++)
    {
        int index2;
        int nextTileUsed = 0;

        for (index2 = 0; index2 < 1024; index2++)
        {
            if (mapData[index2] == index)
            {
                optimisedMapData[index2] = nextTileNumber;
                nextTileUsed = 1;
            }
        }

        if (nextTileUsed)
            nextTileNumber++;
    }

    printf ("%d tiles in picture\n", nextTileNumber);
}




/***************************************************************************/
/* name: writeFileData                                                     */
/* desc: This writes out the .MAP, .COL and .SET files as required.        */
/***************************************************************************/

void writeFileData (void)
{
    int index;

    if (screenRequired)
    {
        printf ("\nMAP Filename >");
        scanf ("%s", filename);

        if (filename[0] == '.')
        {
            strcpy (filename, gifFilename);
            setExtension (filename, ".MAP", 1);
        }
        else
            setExtension (filename, ".MAP", 0);

        dataFilePtr = fopen (filename, "wb");

        if (dataFilePtr != NULL)
        {
            int palette = 1;

            if (numColours == 16)
            {
                printf ("Palette (1..8) >");
                scanf ("%d", &palette);

                if (palette < 1)
                    palette = 1;

                if (palette > 8)
                    palette = 8;
            }

            palette--;

            /* loop over all character codes on screen */
            for (index = 0; index < 1024; index++)
            {
                unsigned char data;

                /* get low 8 bits of code */
                data = optimisedMapData[index] & 0xFF;

                /* write to file */
                fwrite (&data, 1, 1, dataFilePtr);

                /* get high 3 bits of code */
                data =  (optimisedFlipData[index].hFlip?0x80:0x00) +
                        (optimisedFlipData[index].vFlip?0x40:0x00) +
                        (palette << 2) + (optimisedMapData[index]>>8);

                /* write to file */
                fwrite (&data, 1, 1, dataFilePtr);
            }

            fclose (dataFilePtr);

            printf ("Screen tile map written to file %s\n\n", filename);
        }
        else
            printf ("Error: cannot open file %s\n\n", filename);
    }

    if (coloursRequired)
    {
        printf ("COL Filename >");
        scanf ("%s", filename);

        if (filename[0] == '.')
        {
            strcpy (filename, gifFilename);
            setExtension (filename, ".COL", 1);
        }
        else
            setExtension (filename, ".COL", 0);

        dataFilePtr = fopen (filename, "wb");

        if (dataFilePtr != NULL)
        {
            /* now write out colour data */
            for (index = 0; index < numColours; index++)
            {
                unsigned int colour;
                unsigned char data;

                /* convert from 8-bit RGB to 5-bit RGB */
                colour = (red[index]>>3) | ((green[index]>>3)<<5) | ((blue[index]>>3)<<10);

                /* get low part of colour */
                data = colour & 0xFF;

                /* write to file */
                fwrite (&data, 1, 1, dataFilePtr);

                /* get high part of colour */
                data = colour>>8;

                /* write to file */
                fwrite (&data, 1, 1, dataFilePtr);
            }

            fclose (dataFilePtr);

            printf ("Colour palette data written to file %s\n", filename);
        }
        else
            printf ("Error: cannot open file %s\n\n", filename);
    }

    if (tilesRequired)
    {
        printf ("\nSET Filename >");
        scanf ("%s", filename);

        if (filename[0] == '.')
        {
            strcpy (filename, gifFilename);
            setExtension (filename, ".SET", 1);
        }
        else
            setExtension (filename, ".SET", 0);

        dataFilePtr = fopen (filename, "wb");

        if (dataFilePtr != NULL)
        {
            for (index = 0; index < 1024; index++)
            {
                int index2 = 0;
                int found = 0;

                while ((index2 < 1024) && !found)
                    if (mapData[index2] == index)
                    {
                        unsigned long offset = bytesPerChar * index;
                        fwrite (&(tileData[offset]), bytesPerChar, 1, dataFilePtr);
                        found = 1;
                    }
                    else
                        index2++;
            }

            fclose (dataFilePtr);

            printf ("Tile set data written to file %s\n", filename);
        }
        else
            printf ("Error: cannot open file %s\n", filename);
    }
}




/***************************************************************************/
/* name: main                                                              */
/* desc: Get's the GIF picture filename from the user, calls the functions */
/*       to read the header information, decode the file and convert to    */
/*       SNES data.                                                        */
/***************************************************************************/

int main (int _argc, char** _argv)
{
    int index;

    /* set default options */
    screenRequired = coloursRequired = tilesRequired = flipOptimise = 1;

    /* check command line options */
    for (index = 0; index < _argc; index++)
    {
        if (!strncmp (_argv[index], "-s", 2))
            screenRequired = 0;
        if (!strncmp (_argv[index], "-c", 2))
            coloursRequired = 0;
        if (!strncmp (_argv[index], "-t", 2))
            tilesRequired = 0;
        if (!strncmp (_argv[index], "-f", 2))
            flipOptimise = 0;
    }

    /* if doing something... */
    if (screenRequired || coloursRequired || tilesRequired)
    {
        /* allocate memory to hold display image */
        displayImage = (unsigned char*)malloc (DISPLAY_BYTES);

        /* check memory allocation */
        if (displayImage == NULL)
            printf ("Error: allocating displayImage memory\n");

        /* and initialise it all to 0 */
        memset (displayImage, 0, 16384);
        memset (&displayImage[16384], 0, 16384);
        memset (&displayImage[32768], 0, 16384);
        memset (&displayImage[49152], 0, 16384);

        /* allocate SNES format data */
        tileData = (unsigned char*)malloc (TILE_DATA_BYTES);
        h_flippedData = (unsigned char*)malloc (TILE_DATA_BYTES);
        v_flippedData = (unsigned char*)malloc (TILE_DATA_BYTES);
        hv_flippedData = (unsigned char*)malloc (TILE_DATA_BYTES);

        /* check memory allocations successful */
        if ((tileData == NULL) || (h_flippedData == NULL) ||
            (v_flippedData == NULL) || (hv_flippedData == NULL))
            printf ("Error: allocating tileData memory\n");

        /* indicate that no colour map read yet */
        red = green = blue = NULL;

        /* prompt and read the filename */
        printf ("\n16/256 Colour GIF to SNES Picture Convertor    Version 1   23/11/93\n\n");
        printf ("GIF filename >");
        scanf ("%s", gifFilename);

        /* make it a .GIF extension if no extension specified */
        setExtension (gifFilename, ".GIF", 0);

        /* open the file */
        pictureFilePtr = fopen (gifFilename, "rb");

        /* if successfully opened */
        if (pictureFilePtr != NULL)
        {
            /* read in all the GIF header stuff */
            if (readHeaderInformation ())
            {
                /* decode the GIF file */
                decoder (imageWidth);

                /* close picture file */
                fclose (pictureFilePtr);

                /* generate SNES data */
                generateSNESData ();

                /* optimise out duplicates in the map data */
                optimiseMap ();

                /* write out optimised data to files */
                writeFileData ();
            }
        }
        else
            printf ("ERROR : cannot open .GIF file\n");

        /* free up all allocated memory */
        free (red);
        free (green);
        free (blue);
        free (tileData);
        free (h_flippedData);
        free (v_flippedData);
        free (hv_flippedData);
        free (displayImage);
    }
    return 0;
}
