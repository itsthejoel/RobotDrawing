#include <stdio.h>
#include <stdlib.h>
#include "rs232.h"
#include "serial.h"

// Constants for font dimensions and spacing
#define BAUD_RATE 115200  
#define MAX_CHARACTERS 256
#define FONT_UNIT_SIZE 18
#define MAX_TEXT_WIDTH 100.0
#define LINE_GAP 5.0
#define SPACE_GAP 10.0

// Structure to define a single stroke in a character
struct Stroke 
{
    int x;
    int y;
    int pen_down;
};

// Structure to hold font data for a character
struct CharacterData 
{
    int ascii_char;
    int stroke_count;
    struct Stroke strokes[100];
    int char_width;
};

void SendCommands (char *buffer);
int loadFontData(FILE *fontFile, struct CharacterData fontData[MAX_CHARACTERS]);
void processTextandCalculateWidth(FILE *textFile, struct CharacterData fontData[MAX_CHARACTERS], int textHeight, int *current_x, int *current_y, int *prev_pen_state);
void generateAndSendGCode(int x, int y, int pen_down, int *previous_pen_state);

int main()
{

    //char mode[]= {'8','N','1',0};
    char buffer[100];

    // If we cannot open the port then give up immediately
    if ( CanRS232PortBeOpened() == -1 )
    {
        printf ("\nUnable to open the COM port (specified in serial.h) ");
        exit (0);
    }

    // Time to wake up the robot
    printf ("\nAbout to wake up the robot\n");

    // We do this by sending a new-line
    sprintf (buffer, "\n");
    // printf ("Buffer to send: %s", buffer); // For diagnostic purposes only, normally comment out
    PrintBuffer (&buffer[0]);
    Sleep(100);

    // This is a special case - we wait  until we see a dollar ($)
    WaitForDollar();

    printf ("\nThe robot is now ready to draw\n");

    //These commands get the robot into 'ready to draw mode' and need to be sent before any writing commands
    sprintf (buffer, "G1 X0 Y0 F1000\n");
    SendCommands(buffer);
    sprintf (buffer, "M3\n");
    SendCommands(buffer);
    sprintf (buffer, "S0\n");
    SendCommands(buffer);

    FILE *fontFile, *textFile;
    struct CharacterData fontData[MAX_CHARACTERS];
    int textHeight;

    // Open and load font data
    fontFile = fopen("SingleStrokeFont.txt", "r");
    if (!fontFile) 
    {
        perror("Error opening font file");
        return EXIT_FAILURE;
    }
    loadFontData(fontFile, fontData);
    fclose(fontFile);

    // Get text file name from user
    char textFileName[50];
    printf("Enter text file name: ");
    scanf("%s", textFileName);

    // Open the text file
    textFile = fopen(textFileName, "r");
    if (!textFile) 
    {
        perror("Error opening text file.");
        return EXIT_FAILURE;
    }

    // Get and validate text height from user
    printf("Enter text height (4-10 mm): ");
    scanf("%d", &textHeight);
    if (textHeight < 4 || textHeight > 10) 
    {
        fprintf(stderr, "Error: Text height must be between 4 and 10 mm.\n");
        return EXIT_FAILURE;
    }

    // Define position and pen state variables
    int current_x = 0, current_y = 0, prev_pen_state = 0;
    // Process the text file and generate G-code
    processTextandCalculateWidth(textFile, fontData, textHeight, &current_x, &current_y, &prev_pen_state);
    fclose(textFile);

    // Before we exit the program we need to close the COM port
    CloseRS232Port();
    printf("Com port now closed\n");

    return (0);
}

// Send the data to the robot - note in 'PC' mode you need to hit space twice
// as the dummy 'WaitForReply' has a getch() within the function.
void SendCommands (char *buffer )
{
    // printf ("Buffer to send: %s", buffer); // For diagnostic purposes only, normally comment out
    PrintBuffer (&buffer[0]);
    WaitForReply();
    Sleep(100); // Can omit this when using the writing robot but has minimal effect
    // getch(); // Omit this once basic testing with emulator has taken place
}

// Function to load font data from a file
int loadFontData(FILE *fontFile, struct CharacterData fontData[MAX_CHARACTERS]) 
{
    int ascii_code, stroke_count, x, y, pen_down, index = 0;

    while (fscanf(fontFile, "%d", &ascii_code) != EOF) 
    {
        if (ascii_code == 999) 
        {
            fscanf(fontFile, "%d %d", &ascii_code, &stroke_count);
            fontData[index].ascii_char = ascii_code;
            fontData[index].stroke_count = stroke_count;

            for (int i = 0; i < stroke_count; ++i) 
            {
                fscanf(fontFile, "%d %d %d", &x, &y, &pen_down);
                fontData[index].strokes[i] = (struct Stroke){x, y, pen_down};
            }
            fontData[index].char_width = fontData[index].strokes[stroke_count - 1].x;
            ++index;
        }
    }
    return 0;
}

void processTextandCalculateWidth(FILE *textFile, struct CharacterData fontData[MAX_CHARACTERS], int textHeight, int *current_x, int *current_y, int *prev_pen_state) 
{
    char currentChar;
    int accumulated_width = 0;

    while ((currentChar = fgetc(textFile)) != EOF) 
    {
        if (currentChar == '\n') 
        {
            // Handle explicit newlines
            *current_x = 0;
            *current_y -= LINE_GAP + textHeight;
            accumulated_width = 0;
        } 
        else if (currentChar == '\r') 
        {
            continue; // Ignore carriage return characters
        } 
        else if (currentChar == ' ') 
        {
            // Handle spaces
            int space_width = SPACE_GAP * textHeight / FONT_UNIT_SIZE;
            accumulated_width += space_width;
            *current_x += space_width;

            if (accumulated_width > MAX_TEXT_WIDTH) 
            {
                *current_x = 0;
                *current_y -= LINE_GAP + textHeight;
                accumulated_width = 0;
            }
        } 
        else 
        {
            // Get character data
            int ascii_value = (int)currentChar;
            struct CharacterData character = fontData[ascii_value];

            if (character.stroke_count == 0) 
            {
                // Skip unsupported characters
                continue;
            }

            // Calculate character width scaled to text height
            int char_width = character.char_width * textHeight / FONT_UNIT_SIZE;

            if (accumulated_width + char_width > MAX_TEXT_WIDTH) 
            {
                // Start a new line if accumulated width exceeds the limit
                *current_x = 0;
                *current_y -= LINE_GAP + textHeight;
                accumulated_width = 0;
            }

            // Generate G-code for the character
            for (int i = 0; i < character.stroke_count; ++i) 
            {
                int x = character.strokes[i].x * textHeight / FONT_UNIT_SIZE;
                int y = character.strokes[i].y * textHeight / FONT_UNIT_SIZE;
                int pen = character.strokes[i].pen_down;

                x += *current_x;
                y += *current_y;
                generateAndSendGCode(x, y, pen, prev_pen_state);
            }

            // Update the accumulated width and current position
            accumulated_width += char_width;
            *current_x += char_width;
        }
    } 
    return 0;
}

// Function to generate and send G-code commands
void generateAndSendGCode(int x, int y, int pen_down, int *previous_pen_state) 
{
    char gcodeBuffer[100];

    if (pen_down != *previous_pen_state) {
        sprintf(gcodeBuffer, "S%d\n", pen_down ? 1000 : 0);
        sendCommands(gcodeBuffer);
        *previous_pen_state = pen_down;
    }

    sprintf(gcodeBuffer, "G%d X%d Y%d\n", pen_down ? 1 : 0, x, y);
    sendCommands(gcodeBuffer);
}