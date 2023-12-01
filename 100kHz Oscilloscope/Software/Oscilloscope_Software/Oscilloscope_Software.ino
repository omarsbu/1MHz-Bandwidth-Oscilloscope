#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Display dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// MCU Pins
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST 8

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite xyplane = TFT_eSprite(&tft);  // xy-plane bitmap 
TFT_eSprite waveform = TFT_eSprite(&tft);  // waveform bitmap (buffer to remove flicker) 
TFT_eSprite text_overlay_buffer = TFT_eSprite(&tft);  // virtual canvas for overwriting text without flicker

// ADC data pins 
const int ADC_pins[] = {36, 39, 34, 35, 32, 33, 25, 26};  // [D7 D6 D5 D4 D3 D2 D1 D0]

// Text parameters
const String word_bank[18] = {  // Common strings that will be shown on display
  " V/DIV",      // 0
  " us/DIV",     // 1
  " ms/DIV",     // 2    
  " s/DIV",      // 3
  "Trigger",     // 4
  "(RISING):",   // 5
  "(FALLING):",  // 6
  " Frequency",  // 7
  " Period",     // 8
  " DutyCycle",  // 9
  " Cycle RMS",  // 10
  " Cursors",    // 11
  " X:",          // 12
  " Y:",          // 13
  " DC OFFSET",   // 14
  "Hz",          // 15
  "kHz",         // 16
  "MHz",         // 17
};

const int text_buffer_size = 10;  // number of characters in text overlay buffer
const int font_width = 6;  // width of a character in chosen font (in pixels)
const int font_height = 8;  // height of a character in chosen font (in pixels)

const int text_coordinates[9][2] = {   // [x coordinates][y coordinates]
  {1,1},  // 0 - volts per division coordinates
  {1 + (11*font_width) + (3*font_width), 1},  // 1 - time per division coordinates
  {1 + (11*font_width) + (6*font_width) + (10*font_width), 1},  // 2 - trigger coordinates  
  {260, 70},   // 3 - frequency coordinates
  {260, 100},   // 4 - period coordinates
  {260, 130},  // 5 - duty cycle coordinates
  {260, 160},  // 6 - cycle RMS coordinates
  {260, 30},   // 7 - XY cursor coordinates
  {260, 190}   // 8 - DC offset coordinates
};

const int text_colors[9] = {
  TFT_GREEN,  // 0 - volts per division color
  TFT_BLUE,     // 1 - time per division color  
  TFT_ORANGE,     // 2 - trigger color
  TFT_MAGENTA,    // 3 - frequency color 
  TFT_MAGENTA,    // 4 - period color
  TFT_MAGENTA,    // 5 - duty cycle color
  TFT_MAGENTA,    // 6 - cycle RMS color
  TFT_RED,      // 7 - XY cursors color 
  TFT_GOLD    // 8 - DC offset color
};

char num2string_buffer[5];  // buffer for number to string conversion
String stringbuffer;  // buffer for stroring strings


// graph dimensions (fixed)
const int numDivisionsX = 10;  // number of divisions of x-axis
const int numDivisionsY = 10;  // number of divisions of y-axis

const int graph_width = 250;  // width of x-axis in pixels 
const int graph_height = 200; // height of y-axis in pixels 

const int divisionWidth = (graph_width + 1) / numDivisionsX;  // width of each division in pixels
const int divisionHeight = (graph_height + 1) / numDivisionsY;  // height of each division in pixels

const int gx = 0 + 1;  // bottom left corner of graph; add 1 to close graph (x-coordinate) 
const int gy = 200 + 1;  // bottom left corner of graph; add 1 to close graph (y-coordinate)


// Vertical Scaling parameters
double volts_per_div = 0.1;  // Volts per vertical division
double ylo = volts_per_div*(-(numDivisionsY/2));  // lower bound of y-axis
double yhi = volts_per_div*(numDivisionsY/2);  // upper bound of y-axis
double yinc = (yhi-ylo)*numDivisionsY;   // Resolution of each division of y-axis


// Horizontal Scaling parameters - Horizontal scaling will be done by varying frequency at which data is read from the ADC 
double us_per_div = 100;  // microseconds per horizontal division 
double ms_per_div;  // milliseconds per horizontal division
double s_per_div;  // seconds per horizontal division
double xlo = 0;  // lower bound of x-axis (always start at 0)
double xhi = us_per_div*numDivisionsX;  // upper bound of x-axis
double xinc = (xhi-xlo)/numDivisionsX;   // Resolution of each division of x-axis


// Plotting parameters
double trigger_level = 0.27; // tigger voltage
uint8_t ADCsamplebuffer[2000]; // samples taken from AD 
double ox , oy;  // most recently plotted xy corrdinates
double x, y;  // xy coordinates to be plotted
boolean graph_reset = true;  // flag to reset starting position of graph when plotting
int x_cursor = 1;
int y_cursor = 1;
// dc offset triangle icon coordinates
const int trig_symbolx1 = 8; int trig_symboly1 = 120;
int trig_symbolx2 = 0; int trig_symboly2 = 124;  
int trig_symbolx3 = 0; int trig_symboly3 = 116;

// Measurement and display scalaing parameters
double frequency = 100;
double period = 10;
double duty_cycle = 50;
double cycle_rms = 3.54;
double dc_offset = 0;

// Function Declarations
void initializeText();
void Graph(TFT_eSprite &d, double x, double y, double gx, double gy, double w, double h, double xlo, double xhi, double xinc, double ylo, double yhi, double yinc, unsigned int gcolor, unsigned int acolor, unsigned int pcolor, unsigned int bcolor, boolean &redraw);
void movetrigIconDown(int value);
void movetrigIconUp(int value);
void changeCyclerms(double value);
void changeDutycycle(double value);
void changeFreq(double value, String unit); 
void changePeriod(double value, String unit); 
void changeVoltsperDiv(double value);
void changeTimeperDiv(double value, String unit);
void changeTriggerLevel(double value);

void poll_ADC_Task(void *parameter);

void setup() {
  Serial.begin(9600);

  tft.init();  
  tft.setRotation(1); // set display orientation
  tft.setTextWrap(false); // Disable text wrap
  tft.fillScreen(TFT_BLACK);  
  tft.setTextSize(1);

  // Configure GPIO pins
  for (int i = 0; i < sizeof(ADC_MSB_nibble) / sizeof(ADC_MSB_nibble[0]); i++) {
    pinMode(ADC_MSB_nibble[i], INPUT);
  }

  for (int i = 0; i < sizeof(ADC_LSB_nibble) / sizeof(ADC_LSB_nibble[0]); i++) {
    pinMode(ADC_LSB_nibble[i], INPUT);
  }

// Task to poll output from ADC
  xTaskCreatePinnedToCore(
    poll_ADC_Task,       // Function to implement the task
    "poll_ADC_Task",     // Name of the task
    10000,              // Stack size (bytes)
    NULL,               // Task parameters
    1,                  // Priority (1 is default)
    NULL,               // Task handle
    0                   // Core to run the task on (0 or 1)
  );
// Task to plot waveform on display
  xTaskCreatePinnedToCore(
    plot_data_Task,       // Function to implement the task
    "plot_data_Task",     // Name of the task
    10000,              // Stack size (bytes)
    NULL,               // Task parameters
    1,                  // Priority (1 is default)
    NULL,               // Task handle
    0                   // Core to run the task on (0 or 1)
  );



  // test signal for plotting (not part of final design)
  for(int i = 0; i < 250; i++)   {
      ADCsamplebuffer[i] = 5*sin((5*i*(6.5/250)));
  }

  waveform.createSprite(graph_width + 1, graph_height + 1); 
  // Clear the canvas and draw the graph lines
  xyplane.createSprite(graph_width + 1, graph_height + 1);  // XY-plane is 250x200 pixels
  xyplane.fillSprite(TFT_BLACK);  // XY-plane has black background

  // draw x-axis divisions
  for (int x = 0; x < xyplane.width(); x += divisionWidth) {
    xyplane.drawFastVLine(x, 0, xyplane.height(), TFT_LIGHTGREY);
  }
  // draw y-axis divisions
  for (int y = 0; y < xyplane.height(); y += divisionHeight) {
    xyplane.drawFastHLine(0, y, xyplane.width(), TFT_LIGHTGREY);
  }
  
  text_overlay_buffer.createSprite(text_buffer_size*font_width, font_height);  // sprite is large enough for a set number of characters
  initializeText();
  tft.fillTriangle(trig_symbolx1, trig_symboly1, trig_symbolx2, trig_symboly2, trig_symbolx3, trig_symboly3, text_colors[8]);

  movetrigIconUp(double(dc_offset/volts_per_div)*(graph_height/numDivisionsY));

  // set ADC parameters:
  analogReadResolution(12);  // Set ADC resolution to 12 bits (0-4095)
  analogSetAttenuation(ADC_0db);  // Set ADC attenuation to DEFAULT (0-1.1V range)
}


void poll_ADC_Task (void *paramter) { // Store 2000 samples from ADC in memory
  (void) parameter; // Unused parameter
  setCpuFrequencyMhz(240);  // Overclock for maximum GPIO sample rate
  for (int i = 0; i < 1999; i++) {
    ADCsamplebuffer[i] = 0;
    for (int j = 0; j < sizeof(ADC_pins) / sizeof(ADC_pins[0]); j++) {
      ADCsamplebuffer[i] = |= (digitalRead(ADC_pins[j] << j));
    }
  }
  vTaskDelay(1); // Small delay to avoid consuming too much CPU time
}


void plot_data_Task (void *paramter) { // Plot data on LCD display

  // Display the canvas on the ILI9341 display
  xyplane.pushToSprite(&waveform,0,0);

  // Map binary values to 0-3.3V range
  for (int i = 0; i < 1999; i++) {    
    ADCsamplebuffer[i] = (double)ADCsamplebuffer[i] / 255 * 3.3; 
  }

  // Poll potentiometer knobs to figure out scaling parameters
  /*  ......
      ......
      ......
  */

  // Figure out which samples to plot....Decimate sequence for higher time/DIV
  /*  ......
      ......
      ......
  */
  
  // Plot samples with graph function...
  /*  ......
      ......
      ......
  */

  waveform.drawFastHLine(0, y_cursor, waveform.width(), TFT_RED);   // Draw Y-cursor
  waveform.drawFastVLine(x_cursor , 0, waveform.height(), TFT_RED);   // Draw X-cursor
  waveform.pushSprite(10,20);
  vTaskDelay(1); // Small delay to avoid consuming too much CPU time
}

void loop() {
  // Display the canvas on the ILI9341 display
  xyplane.pushToSprite(&waveform,0,0);

  while (1) {
    int adc_input = analogRead(36); // read anaolg input from GPIO 36
    double mappedValue = (double)adc_input / 4095.0 * 1.1; // Convert to voltage (0-1.1V)

    Serial.println(mappedValue, 4);

//    if (mappedValue >= (trigger_level - 0.1) && mappedValue <= (trigger_level + 0.1)) {
      for(int i = 0; i < 250; i++) {
        adc_input = analogRead(36); // read anaolg input from GPIO 36
        mappedValue = (double)adc_input / 4095.0 * 1.1; // Convert to voltage (0-1.1V)
        ADCsamplebuffer[i] = mappedValue;

    }
    delay(1000);
        break;
  }

  for (int i = 249; i >= 0; i--) {
    Graph(waveform, i*(xhi/250), (dc_offset + ADCsamplebuffer[i]), gx, gy, graph_width, graph_height, xlo, xhi, xinc, ylo, yhi, yinc, TFT_LIGHTGREY, TFT_RED, TFT_YELLOW, TFT_BLACK, graph_reset);
  }

  waveform.drawFastHLine(0, y_cursor, waveform.width(), TFT_RED);
  waveform.drawFastVLine(x_cursor , 0, waveform.height(), TFT_RED);

  waveform.pushSprite(10,20);
  graph_reset = true;
}




void Graph(TFT_eSprite &d, double x, double y, double gx, double gy, double w, double h, double xlo, double xhi, double xinc, double ylo, double yhi, double yinc, unsigned int gcolor, unsigned int acolor, unsigned int pcolor, unsigned int bcolor, boolean &redraw) {

  double temp;

  if (redraw == true) {
    redraw = false;
    // initialize first points on display
    ox = (x - xlo) * (w) / (xhi - xlo) + gx;
    oy =  (y - ylo) * (-h) / (yhi - ylo) + gy;
  }
  else {
    // compute transform
    x =  (x - xlo) * (w) / (xhi - xlo) + gx;
    y =  (y - ylo) * (-h) / (yhi - ylo) + gy;
    d.drawLine(ox, oy, x, y, pcolor); 
    d.drawLine(ox, oy + 1, x, y + 1, pcolor);
    d.drawLine(ox, oy - 1, x, y - 1, pcolor); 
    ox = x;
    oy = y;
  }
}

void changeVoltsperDiv(double value) {
  volts_per_div = value;
  ylo = volts_per_div*(-(numDivisionsY/2));  
  yhi = volts_per_div*(numDivisionsY/2);  
  yinc = (yhi-ylo)*numDivisionsY;   
  
  tft.fillRect(text_coordinates[0][0], text_coordinates[0][1], 11*font_width, font_height, TFT_BLACK);
  tft.setTextColor(text_colors[0]);
  dtostrf(volts_per_div, 2, 2, num2string_buffer);
  tft.setCursor(text_coordinates[0][0], text_coordinates[0][1]);
  tft.print(num2string_buffer);tft.print(word_bank[0]);
}

void changeTimeperDiv(double value, String unit) {

  xlo = 0;    

  if(unit == "us") {
    us_per_div = value;
    ms_per_div = us_per_div * 0.001;
    s_per_div = ms_per_div * 0.001;
    dtostrf(us_per_div, 3, 0, num2string_buffer);
    
    xhi = us_per_div*numDivisionsX;  
  } 
  else if(unit == "ms") {
    ms_per_div = value;
    us_per_div = ms_per_div * 1000;
    s_per_div = ms_per_div * 0.001;  
    dtostrf(ms_per_div, 3, 0, num2string_buffer);

    xhi = ms_per_div*numDivisionsX;  
  }
  else if(unit == "s") {
    s_per_div = value;
    ms_per_div = ms_per_div * 1000;
    us_per_div = ms_per_div * 1000;  
    dtostrf(s_per_div, 3, 0, num2string_buffer);

    xhi = s_per_div*numDivisionsX;  
  }

  xinc = (xhi-xlo)/numDivisionsX; 
  tft.fillRect(text_coordinates[1][0], text_coordinates[1][1], 10*font_width, font_height, TFT_BLACK);
  tft.setTextColor(text_colors[1]);
  tft.setCursor(text_coordinates[1][0], text_coordinates[1][1]);
  tft.print(num2string_buffer);tft.print(" ");tft.print(unit);tft.print("/DIV");
}


void changeXCursor(int value) {

  if (x_cursor >= graph_width && value >= x_cursor)
    return; 
  else if (x_cursor <= 0 && value <= x_cursor)
    return;

    x_cursor = value;

  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[7][0] + 6, text_coordinates[7][1] + 10);

  tft.setTextColor(text_colors[7]);

  dtostrf(xhi*(double(x_cursor)/(double(graph_width))), 4, 1, num2string_buffer);
  tft.setCursor(text_coordinates[7][0], text_coordinates[7][1] + 10);  
  tft.print(word_bank[12]); tft.print(num2string_buffer); tft.print("us");
}

void changeYCursor(int value) {

  if (y_cursor >= graph_height && value >= y_cursor)
    return; 
  else if (y_cursor <= 0 && value <= y_cursor)
    return;

    y_cursor = value;

    text_overlay_buffer.fillSprite(TFT_BLACK);
    text_overlay_buffer.pushSprite(text_coordinates[7][0] + 6, text_coordinates[7][1] + 20);

    tft.setTextColor(text_colors[7]);
    
    dtostrf((yhi-2*(yhi*(double(y_cursor)/(double(graph_height))))), 4, 2, num2string_buffer);
    tft.setCursor(text_coordinates[7][0], text_coordinates[7][1] + 20);  
    tft.print(word_bank[13]); tft.print(num2string_buffer); tft.print("V");
}

void changeTriggerLevel(double value) {
  trigger_level = value;
  tft.fillRect(text_coordinates[2][0], text_coordinates[2][1], 40*font_width, font_height, TFT_BLACK);
  tft.setTextColor(text_colors[2]);
  dtostrf(trigger_level, 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[2][0], text_coordinates[2][1]);
  tft.print(word_bank[4]);tft.print(word_bank[5]);
  tft.print(num2string_buffer);tft.print(" V");
}

void changeFreq(double value, String unit) {
  frequency = value; 
  tft.setTextColor(text_colors[3]);
  dtostrf(frequency, 5, 2, num2string_buffer);
  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[3][0] + 6, text_coordinates[3][1] + 10);
  tft.setCursor(text_coordinates[3][0] + 6, text_coordinates[3][1] + 10);  
  tft.print(num2string_buffer); tft.print(unit);
}

void changePeriod(double value, String unit) {
  period = value; 
  tft.setTextColor(text_colors[3]);
  dtostrf(period, 5, 2, num2string_buffer);
  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[4][0] + 6, text_coordinates[4][1] + 10);
  tft.setCursor(text_coordinates[4][0] + 6, text_coordinates[4][1] + 10);  
  tft.print(num2string_buffer); tft.print(unit);
}

void changeDutycycle(double value) {
  duty_cycle = value; 
  tft.setTextColor(text_colors[3]);
  dtostrf(duty_cycle, 4, 2, num2string_buffer);
  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[5][0] + 6, text_coordinates[5][1] + 10);
  tft.setCursor(text_coordinates[5][0] + 6, text_coordinates[5][1] + 10);  
  tft.print(num2string_buffer); tft.print("%");
}

void changeCyclerms(double value) {
  cycle_rms = value; 
  tft.setTextColor(text_colors[3]);
  dtostrf(cycle_rms, 4, 2, num2string_buffer);
  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[6][0] + 6, text_coordinates[6][1] + 10);
  tft.setCursor(text_coordinates[6][0] + 6, text_coordinates[6][1] + 10);  
  tft.print(num2string_buffer); tft.print("V");
}

void changeDCoffset(double value) {
  if (value > yhi || value < ylo)
    return;

  if (value > dc_offset)
    movetrigIconUp(double((value-dc_offset)/volts_per_div)*(graph_height/numDivisionsY));
  else if (value < dc_offset)
    movetrigIconUp(double((value - dc_offset)/volts_per_div)*(graph_height/numDivisionsY));

  dc_offset = value;

  tft.setTextColor(text_colors[8]);
  dtostrf(dc_offset, 4, 2, num2string_buffer);
  text_overlay_buffer.fillSprite(TFT_BLACK);
  text_overlay_buffer.pushSprite(text_coordinates[8][0] + 6, text_coordinates[8][1] + 10);
  tft.setCursor(text_coordinates[8][0] + 6, text_coordinates[8][1] + 10);  
  tft.print(num2string_buffer); tft.print("V");
}

void movetrigIconUp(int value) {
    tft.fillTriangle(trig_symbolx1, trig_symboly1, trig_symbolx2, trig_symboly2, trig_symbolx3, trig_symboly3, TFT_BLACK);
    trig_symboly1 -= value;
    trig_symboly2 -= value;    
    trig_symboly3 -= value;
    tft.fillTriangle(trig_symbolx1, trig_symboly1, trig_symbolx2, trig_symboly2, trig_symbolx3, trig_symboly3, text_colors[8]);
}

void movetrigIconDown(int value) {
    tft.fillTriangle(trig_symbolx1, trig_symboly1, trig_symbolx2, trig_symboly2, trig_symbolx3, trig_symboly3, TFT_BLACK);
    trig_symboly1 += value;
    trig_symboly2 += value;    
    trig_symboly3 += value;  
    tft.fillTriangle(trig_symbolx1, trig_symboly1, trig_symbolx2, trig_symboly2, trig_symbolx3, trig_symboly3, text_colors[8]);
}


void initializeText() {

  // Initalize volts/DIV text
  tft.setTextColor(text_colors[0]);
  dtostrf(volts_per_div, 2, 2, num2string_buffer);
  tft.setCursor(text_coordinates[0][0], text_coordinates[0][1]);
  tft.print(num2string_buffer);tft.print(word_bank[0]);

  // Initialize time/DIV text
  tft.setTextColor(text_colors[1]);
  dtostrf(us_per_div, 3, 0, num2string_buffer);
  tft.setCursor(text_coordinates[1][0], text_coordinates[1][1]);
  tft.print(num2string_buffer);tft.print(word_bank[1]);
  
  // Initialize trigger text
  tft.setTextColor(text_colors[2]);
  dtostrf(trigger_level, 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[2][0], text_coordinates[2][1]);
  tft.print(word_bank[4]); tft.print(word_bank[5]); tft.print(num2string_buffer); tft.print(" V");
  
  // Initialize frequency text
  tft.setTextColor(text_colors[3]);
  dtostrf(frequency, 4, 1, num2string_buffer);
  tft.setCursor(text_coordinates[3][0], text_coordinates[3][1]);
  tft.print(word_bank[7]); 
  tft.setCursor(text_coordinates[3][0] + 6, text_coordinates[3][1] + 10);  
  tft.print(num2string_buffer); tft.print(word_bank[16]);

  // Initialize period text
  tft.setTextColor(text_colors[4]);
  dtostrf(period, 4, 1, num2string_buffer);
  tft.setCursor(text_coordinates[4][0], text_coordinates[4][1]);
  tft.print(word_bank[8]); 
  tft.setCursor(text_coordinates[4][0] + 6, text_coordinates[4][1] + 10);  
  tft.print(num2string_buffer); tft.print("us");

  // Initialize duty cycle text
  tft.setTextColor(text_colors[5]);
  dtostrf(duty_cycle, 4, 1, num2string_buffer);
  tft.setCursor(text_coordinates[5][0], text_coordinates[5][1]);
  tft.print(word_bank[9]); 
  tft.setCursor(text_coordinates[5][0] + 6, text_coordinates[5][1] + 10);  
  tft.print(num2string_buffer); tft.print(" %");
  
  // Initialize cycle rms text
  tft.setTextColor(text_colors[6]);
  dtostrf(cycle_rms, 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[6][0], text_coordinates[6][1]);
  tft.print(word_bank[10]); 
  tft.setCursor(text_coordinates[6][0] + 6, text_coordinates[6][1] + 10);  
  tft.print(num2string_buffer); tft.print(" V");

  // Initialize cursor text
  tft.setTextColor(text_colors[7]);
  tft.setCursor(text_coordinates[7][0], text_coordinates[7][1]);
  tft.print(word_bank[11]); 
  // X cursor
  dtostrf(xhi*(x_cursor/graph_width), 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[7][0], text_coordinates[7][1] + 10);  
  tft.print(word_bank[12]); tft.print(num2string_buffer); tft.print("us");
  // Y cursor
  dtostrf(((yhi)-2*(yhi*(double(y_cursor)/(double(graph_height))))), 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[7][0], text_coordinates[7][1] + 20);  
  tft.print(word_bank[13]); tft.print(num2string_buffer); tft.print("V");

  // Initialize dc offset text
  tft.setTextColor(text_colors[8]);
  tft.setCursor(text_coordinates[8][0], text_coordinates[8][1]);
  tft.print(word_bank[14]); 
  dtostrf(dc_offset, 4, 2, num2string_buffer);
  tft.setCursor(text_coordinates[8][0] + 6, text_coordinates[8][1] + 10);  
  tft.print(num2string_buffer); tft.print("V");
}
