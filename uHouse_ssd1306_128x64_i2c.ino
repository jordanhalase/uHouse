/*
 * Copyright (C) 2022 Jordan Halase <jordan@halase.me>
 *
 * Permission to use, copy, modify, and/or distribute this software for any 
 * purpose with or without fee is hereby granted, provided that the above 
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES 
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY 
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES 
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION 
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN 
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/*
 * This project uses a fixed point representation for mesh vertices and their transforms
 * to improve performance. The Atmega328P has no floating point unit so all floating point
 * math would be done in software suffering a performance hit.
 * 
 * The fixed point representation here uses 16-bit signed integers with a 12-bit fractional part.
 * This allows a granularity of ~0.000244 with an integer part in the range [-8, 7].
 */

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define SCREEN_ADDRESS  0x3c              //< This may differ depending on your display module

#define CLOCK_RATE      16000000          //< Change this if you use a clock rate other than 16MHz
#define T1_COMP         (CLOCK_RATE>>8)   //< Prescalar of 256 is equivalent to 8 right shifts (1s timer)

#define SERIAL_PERF                       //< Print performance metrics (FPS) to serial line

/*
 * Draws lines if defined, otherwise draws points
 */
#define DRAW_LINES

/*
 * How far into the screen to render the mesh
 */
#define MESH_DEPTH      0x2a00

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, 4);

typedef struct vec2 {
  int16_t x, y;
} vec2;

typedef struct vec3 {
  int16_t x, y, z;
} vec3;

#define vec2(x, y)    ((vec2){(x), (y)})
#define v2add(a, b)   vec2((a).x+(b).x, (a).y+(b).y)
#define vec3(x, y, z) ((vec3){(x), (y), (z)})

#define NUM_VERTS   57

/*
 * x right, y down, z into screen
 */
static const vec3 mesh_verts[] PROGMEM = {
  // Cube
  vec3( 0x800,  0x800,  0x800),
  vec3(-0x800,  0x800,  0x800),
  vec3(-0x800, -0x800,  0x800),
  vec3( 0x800, -0x800,  0x800),
  vec3( 0x800,  0x800, -0x800),
  vec3(-0x800,  0x800, -0x800),
  vec3(-0x800, -0x800, -0x800),
  vec3( 0x800, -0x800, -0x800),

  // Roof
  vec3( 0x000, -0x1400, 0x000),

  // Door
  vec3(-0x100,  0x800, -0x800),
  vec3(-0x600,  0x800, -0x800),
  vec3(-0x600,  0x200, -0x800),
  vec3(-0x100,  0x200, -0x800),

  // Front window
  vec3( 0x500, -0x200, -0x800),
  vec3( 0x200, -0x200, -0x800),
  vec3( 0x200, -0x500, -0x800),
  vec3( 0x500, -0x500, -0x800),

  // Left window
  vec3(-0x800,  0x500,  0x200),
  vec3(-0x800,  0x500,  0x500),
  vec3(-0x800,  0x200,  0x500),
  vec3(-0x800,  0x200,  0x200),

  // Car
  vec3(-0x800,  0x800,  0xb00),
  vec3( 0x800,  0x800,  0xb00),
  vec3( 0x800,  0x500,  0xb00),
  vec3( 0x400,  0x500,  0xb00),
  vec3( 0x200,  0x200,  0xb00),
  vec3(-0x600,  0x200,  0xb00),
  vec3(-0x800,  0x500,  0xb00),
  vec3(-0x800,  0x800,  0x1200),
  vec3( 0x800,  0x800,  0x1200),
  vec3( 0x800,  0x500,  0x1200),
  vec3( 0x400,  0x500,  0x1200),
  vec3( 0x200,  0x200,  0x1200),
  vec3(-0x600,  0x200,  0x1200),
  vec3(-0x800,  0x500,  0x1200),

  // Tree
  vec3( 0x1000,  0x800,   0x000),
  vec3( 0x1000, -0x1400,  0x000),
  vec3( 0x1000,  0x200,   0x000), // Branch base
  vec3( 0x1400, -0x1000,  0x000),
  vec3( 0xc00,  -0x1000,  0x000),
  vec3( 0x1000, -0x1000,  0x400),
  vec3( 0x1000, -0x1000, -0x400),

  // Fence
  vec3(-0x800,   0x800,   0x000),
  vec3(-0x1400,  0x800,   0x000),
  vec3(-0x1400,  0x200,   0x000),
  vec3(-0x1200,  0x000,   0x000),
  vec3(-0x1000,  0x200,   0x000),
  vec3(-0xe00,   0x000,   0x000),
  vec3(-0xc00,   0x200,   0x000),
  vec3(-0xa00,   0x000,   0x000),
  vec3(-0x800,   0x200,   0x000),
  vec3(-0x1000,  0x800,   0x000),
  vec3(-0xc00,   0x800,   0x000),

  // Welcome mat
  vec3(-0x100,  0x800, -0x900),
  vec3(-0x600,  0x800, -0x900),
  vec3(-0x600,  0x800, -0xc00),
  vec3(-0x100,  0x800, -0xc00)
};

#define NUM_INDICES   136

static const uint8_t mesh_indices[] PROGMEM = {
  0, 1, 1, 2, 2, 3, 3, 0,
  4, 5, 5, 6, 6, 7, 7, 4,
  0, 4, 1, 5, 2, 6, 3, 7,
  2, 8, 3, 8, 6, 8, 7, 8,           // Roof
  10, 11, 11, 12, 12, 9,            // Door
  13, 14, 14, 15, 15, 16, 16, 13,   // Front window
  17, 18, 18, 19, 19, 20, 20, 17,   // Left window
  21, 22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 21, // Car inner side
  28, 29, 29, 30, 30, 31, 31, 32, 32, 33, 33, 34, 34, 28, // Car outer side
  21, 28, 22, 29, 23, 30, 24, 31, 25, 32, 26, 33, 27, 34, // Car body
  35, 36, 37, 38, 37, 39, 37, 40, 37, 41,                 // Tree
  42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 50, 50, 42, 46, 51, 48, 52, // Fence
  53, 54, 54, 55, 55, 56, 56, 53    // Welcome mat
};

/*
 * Screen space vertices
 */
vec2 screen_verts[NUM_VERTS];

/*
 * Constant rotation vector of 2 degrees per frame
 * Uses floating point only to precache the fixed point vector
 */
const vec2 rot0 = vec2(
  lroundf(4096.0f*cosf(3.0f*M_PI/180.0f)),
  lroundf(4096.0f*sinf(3.0f*M_PI/180.0f))
);

/*
 * Rotation vector, updated per-frame
 */
vec2 rotation = vec2(0x1000, 0);

const vec2 loc0 = vec2(
  lroundf(4096.0f*cosf(1.0f*M_PI/180.0f)),
  lroundf(4096.0f*sinf(1.0f*M_PI/180.0f))
);

/*
 * Rotation vector to move the model horizontally
 * across the screen in a sinusoidal fashion
 */
vec2 location = vec2(0x1000, 0);

/*
 * Counts the number of times the rotation vectors are updated
 * and used to reset to vec2(0x1000, 0) each revolution
 * to avoid precision loss
 */
uint16_t rotation_counter = 0;
uint16_t location_counter = 0;

/*
 * Used for performance metrics
 */
uint8_t fps_counter = 0;
uint8_t fps_display = 0;

/*
 * We are using sprintf to cache the top text so the buffer
 * MUST be large enough for all the text or it will overflow!
 * 
 * This is unused for now due to the inefficient text rendering
 * code of the default GFX library.
 */
#define TEXT_LENGTH   20
char *top_text;

void setup() {
  Serial.begin(19200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.cp437(true);

  top_text = (char*)calloc(TEXT_LENGTH+1, sizeof(char));
  if (!top_text) {
    Serial.println(F("Top text allocation failed"));
    for (;;);
  }
  sprintf(top_text, "V:%d L:%d FPS:", NUM_VERTS, NUM_INDICES>>1);

  // Set Timer1 prescalar to 256
  TCCR1A = 0;
  TCCR1B |=  (1 << CS12);
  TCCR1B &= ~(1 << CS11);
  TCCR1B &= ~(1 << CS10);

  // Reset Timer1
  TCNT1 = 0;
  OCR1A = T1_COMP;

  // Enable Timer1 compare interrupt
  TIMSK1 = 1 << OCIE1A;

  // Enable interrupts
  sei();
}

/*
 * Perform a fixed point rotation using complex multiplication
 * with a 32-bit intermediary
 */
static vec2 rotate(const vec2 v1, const vec2 v2)
{
  return vec2(
    (int16_t)(((int32_t)v1.x*v2.x - (int32_t)v1.y*v2.y) >> 12),
    (int16_t)(((int32_t)v1.x*v2.y + (int32_t)v1.y*v2.x) >> 12)
  );
}

void loop() {
  /*
   * Rotate the rotation vector
   */
  rotation = rotate(rotation, rot0);
  location = rotate(location, loc0);

  /*
   * Reset the rotation vectors each revolution to avoid precision loss
   */
  if (++rotation_counter >= 120) {
    rotation_counter = 0;
    rotation = vec2(0x1000, 0);
  }
  if (++location_counter >= 360) {
    location_counter = 0;
    location = vec2(0x1000, 0);
  }

  /*
   * Transform vertices from model space into screen space
   */
  for (uint8_t i = 0; i < NUM_VERTS; ++i) {
    vec3 vertex = {0};
    memcpy_P(&vertex, &mesh_verts[i], sizeof(vec3));
    const int16_t x0 = vertex.x;
    const int16_t y0 = vertex.y;
    const int16_t z0 = vertex.z;

    /*
     * Rotate mesh and move up and down
     */
    const int16_t x = ((((int32_t)x0*rotation.x - (int32_t)z0*rotation.y)) >> 12) + location.y;
    const int16_t z = ((((int32_t)x0*rotation.y + (int32_t)z0*rotation.x)) >> 12) + location.x;
    const int16_t y = y0 + (location.x >> 2);

    const int16_t z_prime = (z + MESH_DEPTH) >> 6;
    const vec2 perspective_divided = vec2(x/z_prime, y/z_prime);
    const vec2 screen_offset = vec2(SCREEN_WIDTH>>1, SCREEN_HEIGHT>>1);
    screen_verts[i] = v2add(perspective_divided, screen_offset);
  }

  display.clearDisplay();
  
#ifdef DRAW_LINES
  /*
   * Draw lines between each vertex
   */
  for (uint8_t i = 0; i < NUM_INDICES; i += 2) {
    const uint8_t i0 = pgm_read_byte(&mesh_indices[i]);
    const uint8_t i1 = pgm_read_byte(&mesh_indices[i+1]);
    const int16_t x1 = screen_verts[i0].x;
    const int16_t y1 = screen_verts[i0].y;
    const int16_t x2 = screen_verts[i1].x;
    const int16_t y2 = screen_verts[i1].y;
    display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }
#else
  /*
   * Draw points for each vertex
   */
  for (uint8_t i = 0; i < NUM_VERTS; ++i) {
    display.drawPixel(screen_verts[i].x, screen_verts[i].y, SSD1306_WHITE);
  }
#endif

  display.display();

  ++fps_counter;
}

/*
 * Interrupt handler for the FPS counter
 */
ISR(TIMER1_COMPA_vect) {
  TCNT1 = 0;
  fps_display = fps_counter;
  fps_counter = 0;
#ifdef SERIAL_PERF
  Serial.println(fps_display);
#endif
}
