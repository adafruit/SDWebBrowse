/*
 * USERS OF ARDUINO 0023 AND EARLIER: use the 'SDWebBrowse.pde' sketch...
 * 'SDWebBrowse.ino' can be ignored.
 * USERS OF ARDUINO 1.0 AND LATER: **DELETE** the 'SDWebBrowse.pde' sketch
 * and use ONLY the 'SDWebBrowse.ino' file.  By default, BOTH files will
 * load when using the Sketchbook menu, and the .pde version will cause
 * compiler errors in 1.0.  Delete the .pde, then load the sketch.
 *
 * I can't explain WHY this is necessary, but something among the various
 * libraries here appears to be wreaking inexplicable havoc with the
 * 'ARDUINO' definition, making the usual version test unusable (BOTH
 * cases evaluate as true).  FML.
 */

/*
 * This sketch uses the microSD card slot on the Arduino Ethernet shield
 * to serve up files over a very minimal browsing interface
 *
 * Some code is from Bill Greiman's SdFatLib examples, some is from the
 * Arduino Ethernet WebServer example and the rest is from Limor Fried
 * (Adafruit) so its probably under GPL
 *
 * Tutorial is at http://www.ladyada.net/learn/arduino/ethfiles.html
 * Pull requests should go to http://github.com/adafruit/SDWebBrowse
 */

#include <SdFat.h>
#include <SdFatUtil.h>
#include <Ethernet.h>
#include <SPI.h>

/************ ETHERNET STUFF ************/
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
byte ip[] = { 192, 168, 0, 80 };
EthernetServer server(80);

/************ SDCARD STUFF ************/
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile file;

// store error strings in flash to save RAM
#define error(s) error_P(PSTR(s))

void error_P(const char* str) {
  PgmPrint("error: ");
  SerialPrintln_P(str);
  if (card.errorCode()) {
    PgmPrint("SD error: ");
    Serial.print(card.errorCode(), HEX);
    Serial.print(',');
    Serial.println(card.errorData(), HEX);
  }
  while(1);
}

void setup() {
  Serial.begin(9600);
 
  PgmPrint("Free RAM: ");
  Serial.println(FreeRam());  
  
  // initialize the SD card at SPI_HALF_SPEED to avoid bus errors with
  // breadboards.  use SPI_FULL_SPEED for better performance.
  pinMode(10, OUTPUT);                       // set the SS pin as an output (necessary!)
  digitalWrite(10, HIGH);                    // but turn off the W5100 chip!

  if (!card.init(SPI_HALF_SPEED, 4)) error("card.init failed!");
  
  // initialize a FAT volume
  if (!volume.init(&card)) error("vol.init failed!");

  PgmPrint("Volume is FAT");
  Serial.println(volume.fatType(),DEC);
  Serial.println();
  
  if (!root.openRoot(&volume)) error("openRoot failed");

  // list file in root with date and size
  PgmPrintln("Files found in root:");
  root.ls(LS_DATE | LS_SIZE);
  Serial.println();
  
  // Recursive list of all directories
  PgmPrintln("Files found in all dirs:");
  root.ls(LS_R);
  
  Serial.println();
  PgmPrintln("Done");
  
  // Debugging complete, we start the server!
  Ethernet.begin(mac, ip);
  server.begin();
}

void ListFiles(EthernetClient client, uint8_t flags, SdFile dir) {
  // This code is just copied from SdFile.cpp in the SDFat library
  // and tweaked to print to the client output in html!
  dir_t p;
  
  dir.rewind();
  client.println("<ul>");
  while (dir.readDir(&p) > 0) {
    // done if past last used entry
    if (p.name[0] == DIR_NAME_FREE) break;

    // skip deleted entry and entries for . and  ..
    if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') continue;

    // only list subdirectories and files
    if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;

    // print any indent spaces
    client.print("<li><a href=\"");
    for (uint8_t i = 0; i < 11; i++) {
      if (p.name[i] == ' ') continue;
      if (i == 8) {
        client.print('.');
      }
      client.print((char)p.name[i]);
    }
    if (DIR_IS_SUBDIR(&p)) {
      client.print('/');
    }
    client.print("\">");
    
    // print file name with possible blank fill
    for (uint8_t i = 0; i < 11; i++) {
      if (p.name[i] == ' ') continue;
      if (i == 8) {
        client.print('.');
      }
      client.print((char)p.name[i]);
    }
    if (DIR_IS_SUBDIR(&p)) {
      client.print('/');
    }
    client.print("</a>");

    // print modify date/time if requested
    if (flags & LS_DATE) {
       dir.printFatDate(p.lastWriteDate);
       client.print(' ');
       dir.printFatTime(p.lastWriteTime);
    }
    // print size if requested
    if (!DIR_IS_SUBDIR(&p) && (flags & LS_SIZE)) {
      client.print(' ');
      client.print(p.fileSize);
    }
    client.println("</li>");
  }
  client.println("</ul>");
}

// How big our line buffer should be. 100 is plenty!
#define BUFSIZ 100

void loop()
{
  char clientline[BUFSIZ];
  char name[17];
  int index = 0;
  
  EthernetClient client = server.available();
  if (client) {
    // an http request ends with a blank line
    boolean current_line_is_blank = true;
    
    // reset the input buffer
    index = 0;
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        
        // If it isn't a new line, add the character to the buffer
        if (c != '\n' && c != '\r') {
          clientline[index] = c;
          index++;
          // are we too big for the buffer? start tossing out data
          if (index >= BUFSIZ) 
            index = BUFSIZ -1;
          
          // continue to read more data!
          continue;
        }
        
        // got a \n or \r new line, which means the string is done
        clientline[index] = 0;
        
        // Print it out for debugging
        Serial.println(clientline);
        
        // Look for substring such as a request to get the root file
        if (strstr(clientline, "GET / ") != 0) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();
          
          // print all the files, use a helper to keep it clean
          client.println("<h2>Files:</h2>");
          ListFiles(client, LS_SIZE, root);
        } else if (strstr(clientline, "GET /") != 0) {
          // this time no space after the /, so a sub-file!
          char *filename;
          
          filename = clientline + 5; // look after the "GET /" (5 chars)
          // a little trick, look for the " HTTP/1.1" string and 
          // turn the first character of the substring into a 0 to clear it out.
          (strstr(clientline, " HTTP"))[0] = 0;
          
          // print the file we want
          Serial.println(filename);

          if (! file.open(&root, filename, O_READ)) {
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-Type: text/html");
            client.println();
            client.println("<h2>File Not Found!</h2>");
            client.println("<br><h3>Couldn't open the File!</h3>");
            break;
          }
          
          Serial.println("Opened!");
                    
          client.println("HTTP/1.1 200 OK");
          if(file.isDir()) {
            Serial.println("is directory");
            //file.close();
            client.println("Content-Type: text/html");
            client.println();
            client.print("<h2>Files in /");
            file.getFilename(name);
            client.print(name);
            client.println("/:</h2>");
            ListFiles(client,LS_SIZE,file);
            file.close();
          }
          else {
            client.println("Content-Type: text/plain");
            client.println();
          
            int16_t c;
            while ((c = file.read()) > 0) {
              // uncomment the serial to debug (slow!)
              //Serial.print((char)c);
              client.print((char)c);
            }
            file.close();
          }
        } else {
          // everything else is a 404
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/html");
          client.println();
          client.println("<h2>File Not Found!</h2>");
        }
        break;
      }
    }
    // give the web browser time to receive the data
    delay(1);
    client.stop();
  }
}
