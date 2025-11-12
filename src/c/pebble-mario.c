// Copyright (C) 2013 Denis Dzyubenko
// Copyright (C) 2015 Alexey Avdyukhin
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// You may contact the author of the original watchface at denis@ddenis.info
// Author of the color mod for Pebble Time: clusterrr@clusterrr.com
//

#include <pebble.h>
#include <time.h>

//#define DEMO // display fake time. Good for taking screenshots of the watchface.
//#define DEMO_SLOW // slow movements

static Window *window;

static Layer *blocks_layer;
static Layer *mario_layer;
static Layer *time_layer;
static Layer *background_layer;
static Layer *battery_layer;
static Layer *phone_battery_layer;

static char hour_text[3];
static char minute_text[3];
static char hour_text_visible[]   = "  ";
static char minute_text_visible[] = "  ";
static char date_text[12];

static GRect blocks_down_rect;
static GRect blocks_up_rect;
static GRect mario_down_rect;
static GRect mario_up_rect;
static GRect background_rect;
static GRect battery_rect;
static GRect phone_battery_rect;
static GRect time_up_rect;
static GRect time_normal_rect;
static GRect time_down_rect;

static GFont pixel_font;
static GFont pixel_font_small;

static int mario_is_down = 1;
static int need_update_background = 0;

static GBitmap *mario_normal_bmp = NULL;
static GBitmap *mario_jump_bmp = NULL;
static GBitmap *block_bmp = NULL;
static GBitmap *no_phone_bmp = NULL;
static GBitmap *phone_battery_bmp = NULL;
static GBitmap *phone_battery_unknown_bmp = NULL;
static GBitmap *watch_bmp = NULL;
static GBitmap *battery_charging_bmp = NULL;
static GBitmap *background_day_bmp = NULL;
static GBitmap *weather_icon_bmp = NULL;

static PropertyAnimation *mario_animation_beg = NULL;
static PropertyAnimation *mario_animation_end = NULL;

static PropertyAnimation *block_animation_beg = NULL;
static PropertyAnimation *block_animation_end = NULL;

static PropertyAnimation *hour_animation_slide_away = NULL;
static PropertyAnimation *hour_animation_slide_in = NULL;

static bool config_show_no_phone = true;
static bool config_show_battery = true;
static bool config_show_phone_battery = false;
static bool config_show_weather = true;
static bool config_vibe = false;
static int config_character = 0;
static int config_background = 0;
static int phone_battery_level = -1;
static bool config_vibe_hour = false;

static time_t weather_last_update = 0;
static int weather_icon_id = -1;
static int weather_temperature = -100;

static int left_info_mode = 0;

static char digits[10][15] = {{1,1,1,1,0,1,1,0,1,1,0,1,1,1,1},{0,0,1,0,0,1,0,0,1,0,0,1,0,0,1},
                            {1,1,1,0,0,1,1,1,1,1,0,0,1,1,1},{1,1,1,0,0,1,1,1,1,0,0,1,1,1,1},
                            {1,0,1,1,0,1,1,1,1,0,0,1,0,0,1},{1,1,1,1,0,0,1,1,1,0,0,1,1,1,1},
                            {1,1,1,1,0,0,1,1,1,1,0,1,1,1,1},{1,1,1,0,0,1,0,0,1,0,0,1,0,0,1},
                            {1,1,1,1,0,1,1,1,1,1,0,1,1,1,1},{1,1,1,1,0,1,1,1,1,0,0,1,1,1,1}};

#define BLOCK_SIZE 50
#define BLOCK_SPACING 0
#ifdef DEMO_SLOW
#define MARIO_JUMP_DURATION 5000
#define CLOCK_ANIMATION_DURATION 5000
#else
#define MARIO_JUMP_DURATION 150
#define CLOCK_ANIMATION_DURATION 150
#endif
#define GROUND_HEIGHT 26
  
#define WEATHER_MAX_AGE 60*60*3
#define WEATHER_UPDATE_INTERVAL 60*60*1

#define MSG_SHOW_NO_PHONE 0
#define MSG_SHOW_BATTERY 1
#define MSG_VIBE 2
#define MSG_BATTERY_REQUEST 4
#define MSG_BATTERY_ANSWER 5
#define MSG_SHOW_PHONE_BATTERY 6
#define MSG_BACKGROUND 7
#define MSG_SHOW_WEATHER 8
#define ID_WEATHER_LAST_UPDATE 9
#define MSG_WEATHER_ICON_ID 10
#define MSG_WEATHER_TEMPERATURE 11
#define MSG_WEATHER_REQUEST 12
#define ID_LEFT_INFO_MODE 13
#define MSG_VIBE_HOUR 14
#define MSG_CHARACTER 15
#ifdef DEMO
  #define MARIO_TIME_UNIT SECOND_UNIT
#else
  #define MARIO_TIME_UNIT MINUTE_UNIT
#endif

void handle_tick(struct tm *tick_time, TimeUnits units_changed);

static void to_upcase(char* str)
{
  while (*str)
  {
    if (*str >='a' && *str <= 'z') *str += 'A'-'a';
    str++;
  }
}

static void request_all()
{
  int weather_age = time(NULL)-weather_last_update;

  if (config_show_phone_battery || (config_show_weather && ((weather_age > WEATHER_UPDATE_INTERVAL))))
  {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    if (config_show_phone_battery)
    {
      Tuplet tupleRequest = TupletInteger(MSG_BATTERY_REQUEST, 0);
      dict_write_tuplet(iter, &tupleRequest);
    }
    if (config_show_weather && (weather_age > WEATHER_UPDATE_INTERVAL))
    {
      Tuplet tupleRequest = TupletInteger(MSG_WEATHER_REQUEST, 0);
      dict_write_tuplet(iter, &tupleRequest);
    }
    app_message_outbox_send();
  }
}

static void request_all_on_connect(void* data)
{
 request_all();
}

static void request_all_on_start(void* data)
{
  tick_timer_service_subscribe(MARIO_TIME_UNIT, handle_tick);  
  request_all();
}

void time_update_callback(Layer *layer, GContext *ctx)
{
  GRect layer_bounds = layer_get_bounds(layer);
  char h1[2];
  char h2[2];
  char m1[2];
  char m2[2];
  h1[0] = hour_text_visible[0];
  h2[0] = hour_text_visible[1];
  m1[0] = minute_text_visible[0];
  m2[0] = minute_text_visible[1];
  h1[1] = h2[1] = m1[1] = m2[1] = 0;
  layer_bounds.origin.y += 1;
  int one_fix = 2;
#if PBL_COLOR  
  GRect origin = layer_bounds;
  graphics_context_set_text_color(ctx, GColorBlack);
  layer_bounds.origin.x += 4;
  layer_bounds.origin.y += 1;
  if (h1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, h1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (h2[0] == '1') layer_bounds.origin.x += one_fix;
  graphics_draw_text(ctx, h2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h2[0] == '1') layer_bounds.origin.x -= one_fix;
  layer_bounds.origin.x += 30;
  if (m1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, m1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (m1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (m2[0] == '1') layer_bounds.origin.x += one_fix;
  graphics_draw_text(ctx, m2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  layer_bounds = origin;
  graphics_context_set_text_color(ctx, GColorBulgarianRose);
  layer_bounds.origin.x += 3;
  if (h1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, h1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (h2[0] == '1') layer_bounds.origin.x += one_fix;
  graphics_draw_text(ctx, h2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h2[0] == '1') layer_bounds.origin.x -= one_fix;
  layer_bounds.origin.x += 30;
  if (m1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, m1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (m1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (m2[0] == '1') layer_bounds.origin.x += one_fix;
  graphics_draw_text(ctx, m2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#else
  graphics_context_set_text_color(ctx, GColorBlack);
  layer_bounds.origin.x += 3;
  if (h1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, h1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (h2[0] == '1') layer_bounds.origin.x += one_fix;
  graphics_draw_text(ctx, h2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (h2[0] == '1') layer_bounds.origin.x -= one_fix;
  layer_bounds.origin.x += 30;
  if (m1[0] == '1') layer_bounds.origin.x -= one_fix;
  graphics_draw_text(ctx, m1, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (m1[0] == '1') layer_bounds.origin.x += one_fix;
  layer_bounds.origin.x += 20;
  if (m2[0] == '1') layer_bounds.origin.x += 3;
  graphics_draw_text(ctx, m2, pixel_font, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
#endif
}

void blocks_update_callback(Layer *layer, GContext *ctx)
{
  GRect block_rect[2];

  GRect layer_bounds = layer_get_bounds(layer);
  GRect layer_frame = layer_get_frame(layer);

  block_rect[0] = GRect(layer_bounds.origin.x,
              layer_bounds.origin.y + 4,
              BLOCK_SIZE,
              layer_frame.size.h - 4);
  block_rect[1] = GRect(layer_bounds.origin.x + BLOCK_SIZE + BLOCK_SPACING,
              layer_bounds.origin.y + 4,
              BLOCK_SIZE,
              layer_frame.size.h - 4);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  for (uint8_t i = 0; i < 2; ++i) {
    GRect *rect = block_rect + i;
    graphics_draw_bitmap_in_rect(ctx, block_bmp, *rect);
  }
}

void mario_update_callback(Layer *layer, GContext *ctx)
{
  GRect destination;
  GBitmap *bmp;

  bmp = mario_is_down ? mario_normal_bmp : mario_jump_bmp;
  GRect bounds = gbitmap_get_bounds(bmp);
  destination = GRect(40-bounds.size.w/2, 80-bounds.size.h, bounds.size.w, bounds.size.h);

  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, destination);
}

// Draw background and date
void ground_update_callback(Layer *layer, GContext *ctx)
{
  GRect layer_bounds = layer_get_bounds(layer);

  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  graphics_draw_bitmap_in_rect(ctx, background_day_bmp, layer_bounds);

#if !PBL_PLATFORM_CHALK
  layer_bounds.origin.y = 5;
  layer_bounds.origin.x = 31;
#else
  layer_bounds.origin.y = 13;
  layer_bounds.origin.x = 31+18;  
#endif

  #if PBL_COLOR  
  graphics_context_set_text_color(ctx, GColorWhite);
#else
  graphics_context_set_text_color(ctx, GColorBlack);
#endif

  time_t t;
  time(&t); 
  struct tm * tick_time = localtime(&t);
  
  // Compress spaces
  strftime(date_text, sizeof(date_text), "%a,", tick_time);
  to_upcase(date_text);
  graphics_draw_text(ctx, date_text, pixel_font_small, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  layer_bounds.origin.x += 34;
  strftime(date_text, sizeof(date_text), "%b", tick_time);
  to_upcase(date_text);
  graphics_draw_text(ctx, date_text, pixel_font_small, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  layer_bounds.origin.x += 30;
  strftime(date_text, sizeof(date_text), "%d", tick_time);
  to_upcase(date_text);
  graphics_draw_text(ctx, date_text, pixel_font_small, layer_bounds, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void bluetooth_connection_callback(bool connected)
{
  if (config_vibe && !connected) {  
    static const uint32_t const segments[] = { 100, 200, 100, 200, 100 };
    VibePattern pat = {
      .durations = segments,
      .num_segments = ARRAY_LENGTH(segments),
    };
    vibes_enqueue_custom_pattern(pat);
  }
  
  if (connected)
    app_timer_register(5000, request_all_on_connect, NULL);
  else phone_battery_level = -1;
  layer_mark_dirty(phone_battery_layer);
}

void phone_battery_update_callback(Layer *layer, GContext *ctx)
{
#ifdef DEMO
  phone_battery_level = 8;
#endif
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
#if PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
#else
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
#endif

  if (config_show_no_phone && !bluetooth_connection_service_peek())
  {
    GRect image_rect = gbitmap_get_bounds(no_phone_bmp);
    image_rect.origin.y += 2;
    graphics_draw_bitmap_in_rect(ctx, no_phone_bmp, image_rect);
    return;
  }
  
  if (config_show_weather && (!config_show_phone_battery || left_info_mode == 0))
  {
    GRect image_rect = gbitmap_get_bounds(weather_icon_bmp);
    image_rect.origin.y += (13-image_rect.size.h)/2;
    graphics_draw_bitmap_in_rect(ctx, weather_icon_bmp, image_rect);
    
    if (weather_temperature > -100)
    {
      int temp_x = image_rect.size.w + 4; //13;
      if (temp_x > 13) temp_x = 13;
      int temp_y = 4;
    
      int digit1 = (weather_temperature / 10) % 10;
      if (digit1 < 0) digit1 *= -1;      
      int digit2 = weather_temperature % 10;
      if (digit2 < 0) digit2 *= -1;
    
      int dx, dy;
      for (dy = 0; dy < 5; dy++)
      {
        for (dx = 0; dx < 3; dx++)
        {
          if (digits[digit1][dx+dy*3])
            graphics_draw_pixel(ctx, GPoint(temp_x+dx, temp_y+dy));
          if (digits[digit2][dx+dy*3])
            graphics_draw_pixel(ctx, GPoint(temp_x+dx+4, temp_y+dy));
        }
      }
      graphics_draw_pixel(ctx, GPoint(temp_x+4+4, temp_y-2));
      if (weather_temperature < 0)
      {
        graphics_draw_pixel(ctx, GPoint(temp_x-2, temp_y+2));
        graphics_draw_pixel(ctx, GPoint(temp_x-3, temp_y+2));
      }
    }
  }

  if (config_show_phone_battery && (!config_show_weather || left_info_mode != 0))
  {
    if (phone_battery_level >= 0)
    {
      GRect image_rect = gbitmap_get_bounds(phone_battery_bmp);
      image_rect.origin.y += 2;
      graphics_draw_bitmap_in_rect(ctx, phone_battery_bmp, image_rect);
      graphics_fill_rect(ctx, GRect(9, 4, phone_battery_level, 5), 0, GCornerNone);
    } else {
      GRect image_rect = gbitmap_get_bounds(phone_battery_unknown_bmp);
      image_rect.origin.y += 2;
      graphics_draw_bitmap_in_rect(ctx, phone_battery_unknown_bmp, image_rect);
    }
  }
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction)
{  
  left_info_mode ^= 1;
  layer_mark_dirty(phone_battery_layer);
  persist_write_int(ID_LEFT_INFO_MODE, left_info_mode);
}

void battery_update_callback(Layer *layer, GContext *ctx)
{
  if (config_show_battery)
  {
    GRect image_rect = gbitmap_get_bounds(watch_bmp);
    BatteryChargeState charge_state = battery_state_service_peek();
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    if (!charge_state.is_charging)
      graphics_draw_bitmap_in_rect(ctx, watch_bmp, image_rect);
    else
      graphics_draw_bitmap_in_rect(ctx, battery_charging_bmp, image_rect);
#if PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorWhite);
#else
      graphics_context_set_fill_color(ctx, GColorBlack);
#endif
    graphics_fill_rect(ctx, GRect(9, 2, charge_state.charge_percent / 10, 5), 0, GCornerNone);
  }
}

void handle_battery(BatteryChargeState charge_state)
{
  layer_mark_dirty(battery_layer);
}

void update_character()
{
  if (mario_normal_bmp)
    gbitmap_destroy(mario_normal_bmp);
  if (mario_jump_bmp)
    gbitmap_destroy(mario_jump_bmp);

  int character = config_character;
  
  // 1st April joke :)
  time_t t;
  time(&t); 
  struct tm * tick_time = localtime(&t);
  if (tick_time->tm_year == 116 && tick_time->tm_mon == 3 && tick_time->tm_mday == 1) character = 2;
  
  switch (character)
  {
#if PBL_COLOR
    case 1:
      mario_normal_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LUIGI_NORMAL);
      mario_jump_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LUIGI_JUMP);
      break;
    case 2:
      mario_normal_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BOWSER_NORMAL);
      mario_jump_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BOWSER_JUMP);
      break;
#endif
    default:
      mario_normal_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MARIO_NORMAL);
      mario_jump_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MARIO_JUMP);
      break;
  }
}

void update_background()
{
  if (background_day_bmp)
  gbitmap_destroy(background_day_bmp);
#if PBL_COLOR
  if (!config_background)
  {
  time_t t;
  time(&t); 
  struct tm * tick_time = localtime(&t);
#ifdef DEMO
  int s = (tick_time->tm_sec + tick_time->tm_min*60) / 15;
  int hour = ((s % 12) / 2 * 4 + 1);
#else
  int hour = tick_time->tm_hour;
#endif
  if (hour < 4)
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_UNDERGROUND);
  else if (hour < 8)
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_CASTLE);
  else if (hour < 20)
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_DAY);
  else
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_NIGHT);
  } else {
  switch (config_background)
  {
    default: 
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_DAY);
    break;
    case 2: 
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_NIGHT);
    break;
    case 3: 
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_UNDERGROUND);
    break;
    case 4: 
    background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_CASTLE);
    break;
  }
  }
#else
  background_day_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND_DAY);   
#endif
}

void load_bitmaps()
{
  if (no_phone_bmp)
    gbitmap_destroy(no_phone_bmp);
  if (phone_battery_bmp)
    gbitmap_destroy(phone_battery_bmp);
  if (watch_bmp)
    gbitmap_destroy(watch_bmp);
  if (battery_charging_bmp)
    gbitmap_destroy(battery_charging_bmp);
  if (block_bmp)
    gbitmap_destroy(block_bmp);

  no_phone_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_NO_PHONE);
  phone_battery_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PHONE_BATTERY);
  phone_battery_unknown_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_PHONE_BATTERY_UNKNOWN);
  watch_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WATCH_BATTERY);
  battery_charging_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BATTERY_CHARGING);
  block_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BLOCK);
  update_character();
  update_background();
}

void load_weather_icon()
{
  int weather_age = time(NULL)-weather_last_update;
  if ((weather_age > WEATHER_MAX_AGE) && (weather_icon_id >= 0))
  {
    weather_icon_id = -1;
    weather_temperature = -100;
  }

#ifdef DEMO
  weather_icon_id = 9;
  weather_temperature = -23;
#endif
  if (weather_icon_bmp)
    gbitmap_destroy(weather_icon_bmp);
  switch (weather_icon_id)
  {
    case 1:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_CLEAR_SKY_DAY);
      break;
    case 101:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_CLEAR_SKY_NIGHT);
      break;
    case 2:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_FEW_CLOUDS_DAY);
      break;
    case 102:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_FEW_CLOUDS_NIGHT);
      break;
    case 3:
    case 103:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_SCATTERED_CLOUDS);
      break;
    case 4:
    case 104:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_BROKEN_CLOUDS);
      break;
    case 9:
    case 109:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_SHOWER_RAIN);
      break;
    case 10:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_RAIN_DAY);
      break;
    case 110:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_RAIN_NIGHT);
      break;
    case 11:
    case 111:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_THUNDERSTORM);
      break;
    case 13:
    case 113:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_SNOW);
      break;
    case 50:
    case 150:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_MIST);
      break;
    default:
      weather_icon_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WEATHER_UNKNOWN);
      break;
  }
}

void in_received_handler(DictionaryIterator *received, void *context) {
//  APP_LOG(APP_LOG_LEVEL_DEBUG, "Received config");
  Tuple *tuple = dict_find(received, MSG_SHOW_NO_PHONE);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_show_no_phone = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_show_no_phone = tuple->value->int8;
    layer_mark_dirty(phone_battery_layer);
    persist_write_bool(MSG_SHOW_NO_PHONE, config_show_no_phone);
  }
  tuple = dict_find(received, MSG_SHOW_BATTERY);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_show_battery = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_show_battery = tuple->value->int8;
    layer_mark_dirty(battery_layer);
    persist_write_bool(MSG_SHOW_BATTERY, config_show_battery);
    request_all();
  }
  tuple = dict_find(received, MSG_SHOW_PHONE_BATTERY);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_show_phone_battery = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_show_phone_battery = tuple->value->int8;
    layer_mark_dirty(phone_battery_layer);
    persist_write_bool(MSG_SHOW_PHONE_BATTERY, config_show_phone_battery);
  }
  tuple = dict_find(received, MSG_SHOW_WEATHER);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_show_weather = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_show_weather = tuple->value->int8;
    layer_mark_dirty(phone_battery_layer);
    persist_write_bool(MSG_SHOW_WEATHER, config_show_weather);
  }
  tuple = dict_find(received, MSG_VIBE);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_vibe = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_vibe = tuple->value->int8;
    persist_write_bool(MSG_VIBE, config_vibe);
  }
  tuple = dict_find(received, MSG_VIBE_HOUR);
  if (tuple) {
    if (tuple->type == TUPLE_CSTRING)
      config_vibe_hour = (strcmp(tuple->value->cstring, "true") == 0);
    else
      config_vibe_hour = tuple->value->int8;
    persist_write_bool(MSG_VIBE_HOUR, config_vibe_hour);
  }
  tuple = dict_find(received, MSG_BACKGROUND);
  if (tuple) {
    config_background = tuple->value->int8;
    update_background();
    layer_mark_dirty(background_layer);
    persist_write_int(MSG_BACKGROUND, config_background);
  }
  tuple = dict_find(received, MSG_CHARACTER);
  if (tuple) {
    config_character = tuple->value->int8;
    update_character();
    layer_mark_dirty(mario_layer);
    persist_write_int(MSG_CHARACTER, config_character);
  }  
  tuple = dict_find(received, MSG_BATTERY_ANSWER);
  if (tuple) {
    phone_battery_level = tuple->value->int8;
    layer_mark_dirty(phone_battery_layer);
  }
  tuple = dict_find(received, MSG_WEATHER_ICON_ID);
  if (tuple) {
    weather_icon_id = tuple->value->int8;
    tuple = dict_find(received, MSG_WEATHER_TEMPERATURE);
    if (tuple) weather_temperature = tuple->value->int8;
    weather_last_update = time(NULL);
    load_weather_icon();
    layer_mark_dirty(phone_battery_layer);
    persist_write_int(ID_WEATHER_LAST_UPDATE, weather_last_update);
    persist_write_int(MSG_WEATHER_ICON_ID, weather_icon_id);
    persist_write_int(MSG_WEATHER_TEMPERATURE, weather_temperature);
  }
  if (config_show_weather && config_show_phone_battery)
    accel_tap_service_subscribe(accel_tap_handler);
  else
    accel_tap_service_unsubscribe();
}

void handle_init()
{
  if (persist_exists(MSG_SHOW_NO_PHONE))
    config_show_no_phone = persist_read_bool(MSG_SHOW_NO_PHONE);
  if (persist_exists(MSG_SHOW_BATTERY))
    config_show_battery = persist_read_bool(MSG_SHOW_BATTERY);
  if (persist_exists(MSG_SHOW_PHONE_BATTERY))
    config_show_phone_battery = persist_read_bool(MSG_SHOW_PHONE_BATTERY);
  if (persist_exists(MSG_SHOW_WEATHER))
    config_show_weather = persist_read_bool(MSG_SHOW_WEATHER);
  if (persist_exists(MSG_VIBE))
    config_vibe = persist_read_bool(MSG_VIBE);
  if (persist_exists(MSG_VIBE_HOUR))
    config_vibe_hour = persist_read_bool(MSG_VIBE_HOUR);
  if (persist_exists(MSG_BACKGROUND))
    config_background = persist_read_int(MSG_BACKGROUND);  
  if (persist_exists(ID_WEATHER_LAST_UPDATE))
    weather_last_update = persist_read_int(ID_WEATHER_LAST_UPDATE);
  if (persist_exists(MSG_WEATHER_ICON_ID))
    weather_icon_id = persist_read_int(MSG_WEATHER_ICON_ID);
  if (persist_exists(MSG_WEATHER_TEMPERATURE))
    weather_temperature = persist_read_int(MSG_WEATHER_TEMPERATURE);
  if (persist_exists(ID_LEFT_INFO_MODE))
    left_info_mode = persist_read_int(ID_LEFT_INFO_MODE);
  if (persist_exists(MSG_CHARACTER))
    config_character = persist_read_int(MSG_CHARACTER);  

  app_message_register_inbox_received(in_received_handler);
  app_message_open(128, 64);
  window = window_create();

#if !PBL_PLATFORM_CHALK
  int offset_x = 0;
  int offset_y = 0;
  int screen_size_x = 144;
  int screen_size_y = 168;
#else
  int offset_x = 18;
  int offset_y = 6;
  int screen_size_x = 180;
  int screen_size_y = 180;
#endif

  blocks_up_rect = GRect(22+offset_x, -10, BLOCK_SIZE*2, BLOCK_SIZE + 4);
#if PBL_COLOR
  mario_down_rect = GRect(32 + offset_x, 168 - GROUND_HEIGHT-76 + 28 + 10 + offset_y - 32, 80, 80);
  mario_up_rect = GRect(32 + offset_x, BLOCK_SIZE + 4 + 10 + offset_y - 32, 80, 80);
  blocks_down_rect = GRect(22 + offset_x, 25 + offset_y, BLOCK_SIZE*2, BLOCK_SIZE + 4);
#else
  mario_down_rect = GRect(32, 168 - GROUND_HEIGHT - 80 + 10, 80, 80);
  mario_up_rect = GRect(32, BLOCK_SIZE + 4, 80, 80);
  blocks_down_rect = GRect(22, 16, BLOCK_SIZE*2, BLOCK_SIZE + 4);
#endif
  
  background_rect = GRect(0, 0, screen_size_x, screen_size_y);
#if !PBL_PLATFORM_CHALK
  phone_battery_rect = GRect(3, 3, 24, 13);
  battery_rect = GRect(119, 5, 22, 9);
#else
  phone_battery_rect = GRect(3, 3+80, 24, 13);
  battery_rect = GRect(119+36, 5+80, 22, 9);
#endif
  pixel_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_GAMEGIRL_24));
  //pixel_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_EMULOGIC_24));
  pixel_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_EMULOGIC_8));
  
  time_up_rect = GRect(0, -15, BLOCK_SIZE*2, BLOCK_SIZE);
  time_normal_rect = GRect(0, 5 + 4 + 5, BLOCK_SIZE*2, BLOCK_SIZE);
  time_down_rect = GRect(0, BLOCK_SIZE + 4 + 5, BLOCK_SIZE*2, BLOCK_SIZE);

  blocks_layer = layer_create(blocks_down_rect);
  mario_layer = layer_create(mario_down_rect);
  background_layer = layer_create(background_rect);
  battery_layer = layer_create(battery_rect);
  phone_battery_layer = layer_create(phone_battery_rect);
  time_layer = layer_create(time_normal_rect);

  layer_set_update_proc(blocks_layer, &blocks_update_callback);
  layer_set_update_proc(mario_layer, &mario_update_callback);
  layer_set_update_proc(background_layer, &ground_update_callback);
  layer_set_update_proc(battery_layer, &battery_update_callback);
  layer_set_update_proc(phone_battery_layer, &phone_battery_update_callback);
  layer_set_update_proc(time_layer, &time_update_callback);

  Layer *window_layer = window_get_root_layer(window);

  layer_add_child(window_layer, background_layer);
  layer_add_child(background_layer, battery_layer);
  layer_add_child(background_layer, phone_battery_layer);
  layer_add_child(blocks_layer, time_layer);
  layer_add_child(background_layer, blocks_layer);
  layer_add_child(background_layer, mario_layer);
  
  window_stack_push(window, false);

  bluetooth_connection_service_subscribe(bluetooth_connection_callback);
  battery_state_service_subscribe(handle_battery);
  if (config_show_weather && config_show_phone_battery)
    accel_tap_service_subscribe(accel_tap_handler);
  
  app_timer_register(1000, request_all_on_start, NULL);

  load_bitmaps(); 
  load_weather_icon();
#ifdef DEMO
  light_enable(true);
#endif
}

void handle_deinit()
{
  tick_timer_service_unsubscribe();

  animation_unschedule_all();

  if (mario_animation_beg)
    property_animation_destroy(mario_animation_beg);
  if (mario_animation_end)
    property_animation_destroy(mario_animation_end);  
  if (block_animation_beg)
    property_animation_destroy(block_animation_beg);
  if (block_animation_end)
    property_animation_destroy(block_animation_end);
  if (hour_animation_slide_in)
    property_animation_destroy(hour_animation_slide_in);
  if (hour_animation_slide_away)
    property_animation_destroy(hour_animation_slide_away);

  gbitmap_destroy(mario_normal_bmp);
  gbitmap_destroy(mario_jump_bmp);
  gbitmap_destroy(no_phone_bmp);
  gbitmap_destroy(phone_battery_bmp);
  gbitmap_destroy(phone_battery_unknown_bmp);
  gbitmap_destroy(watch_bmp);
  gbitmap_destroy(battery_charging_bmp);  
  gbitmap_destroy(block_bmp);
  gbitmap_destroy(background_day_bmp);
  gbitmap_destroy(weather_icon_bmp);

  layer_destroy(time_layer);
  layer_destroy(background_layer);
  layer_destroy(mario_layer);
  layer_destroy(blocks_layer);
  layer_destroy(battery_layer);
  layer_destroy(phone_battery_layer);

  fonts_unload_custom_font(pixel_font);
  fonts_unload_custom_font(pixel_font_small);
  
  window_destroy(window);
  
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  app_message_deregister_callbacks();
  accel_tap_service_unsubscribe();
}

void mario_down_animation_stopped(Animation *animation, void *data)
{
  mario_is_down = 1;
  layer_mark_dirty(mario_layer);
}

void mario_jump_animation_started(Animation *animation, void *data)
{
  mario_is_down = 0;
  layer_mark_dirty(mario_layer);
}

void mario_jump_animation_stopped(Animation *animation, void *data)
{
  if (mario_animation_end)
    property_animation_destroy(mario_animation_end);

  mario_animation_end = property_animation_create_layer_frame(mario_layer,
                                  &mario_up_rect,
                                  &mario_down_rect);
  animation_set_duration((Animation *)mario_animation_end, MARIO_JUMP_DURATION);
  animation_set_curve((Animation *)mario_animation_end, AnimationCurveEaseIn);
  animation_set_handlers((Animation *)mario_animation_end, (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)mario_down_animation_stopped
  }, 0);

  animation_schedule((Animation *)mario_animation_end);
}

void clock_animation_slide_away_stopped(Animation *animation, void *data)
{
  layer_set_frame((Layer *)time_layer, time_down_rect);
  strcpy(hour_text_visible, hour_text);
  strcpy(minute_text_visible, minute_text);
  layer_mark_dirty(time_layer);

  if (hour_animation_slide_in)
    property_animation_destroy(hour_animation_slide_in);

  hour_animation_slide_in = property_animation_create_layer_frame((Layer *)time_layer,
                                    &time_down_rect,
                                    &time_normal_rect);
  animation_set_duration((Animation *)hour_animation_slide_in, CLOCK_ANIMATION_DURATION);
  animation_set_curve((Animation *)hour_animation_slide_in, AnimationCurveLinear);
  animation_schedule((Animation *)hour_animation_slide_in);
}

void block_up_animation_started(Animation *animation, void *data)
{
  if (hour_animation_slide_away)
    property_animation_destroy(hour_animation_slide_away);
  hour_animation_slide_away = property_animation_create_layer_frame((Layer *)time_layer,
                                      &time_normal_rect,
                                      &time_up_rect);
  animation_set_duration((Animation *)hour_animation_slide_away, CLOCK_ANIMATION_DURATION);
  animation_set_curve((Animation *)hour_animation_slide_away, AnimationCurveLinear);
  animation_set_handlers((Animation *)hour_animation_slide_away, (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)clock_animation_slide_away_stopped
  }, 0);
  animation_schedule((Animation *)hour_animation_slide_away);
}

void block_up_animation_stopped(Animation *animation, void *data)
{
  if (block_animation_end)
    property_animation_destroy(block_animation_end);
  block_animation_end = property_animation_create_layer_frame(blocks_layer,
                                  &blocks_up_rect,
                                  &blocks_down_rect);
  animation_set_duration((Animation *)block_animation_end, MARIO_JUMP_DURATION);
  animation_set_curve((Animation *)block_animation_end, AnimationCurveEaseIn);
  animation_schedule((Animation *)block_animation_end);

  // Update background if need    
  if (need_update_background)
  {
    need_update_background = 0;
    update_background();
  }
  layer_mark_dirty(background_layer);
}

void handle_tick(struct tm *tick_time, TimeUnits units_changed)
{  
#ifdef DEMO
  if (tick_time->tm_sec % 15 != 0) return;
#endif
  if (mario_animation_beg)
    property_animation_destroy(mario_animation_beg);
  mario_animation_beg = property_animation_create_layer_frame(mario_layer,
                                  &mario_down_rect,
                                  &mario_up_rect);
  animation_set_duration((Animation *)mario_animation_beg, MARIO_JUMP_DURATION);
  animation_set_curve((Animation *)mario_animation_beg, AnimationCurveEaseOut);
  animation_set_handlers((Animation *)mario_animation_beg, (AnimationHandlers){
    .started = (AnimationStartedHandler)mario_jump_animation_started,
    .stopped = (AnimationStoppedHandler)mario_jump_animation_stopped
  }, 0);

  if (block_animation_beg)
    property_animation_destroy(block_animation_beg);

  block_animation_beg = property_animation_create_layer_frame(blocks_layer,
                                  &blocks_down_rect,
                                  &blocks_up_rect);
  animation_set_duration((Animation *)block_animation_beg, MARIO_JUMP_DURATION);
#if PBL_COLOR
  animation_set_delay((Animation *)block_animation_beg, MARIO_JUMP_DURATION*4/9);
#else
  animation_set_delay((Animation *)block_animation_beg, MARIO_JUMP_DURATION/10);
#endif
  animation_set_curve((Animation *)block_animation_beg, AnimationCurveEaseOut);
  animation_set_handlers((Animation *)block_animation_beg, (AnimationHandlers){
    .started = (AnimationStartedHandler)block_up_animation_started,
    .stopped = (AnimationStoppedHandler)block_up_animation_stopped
  }, 0);
  
#ifdef DEMO
  int s = (tick_time->tm_sec + tick_time->tm_min*60) / 15;
  snprintf(hour_text, sizeof(hour_text), "%02d", ((s % 12) / 2 * 4 + 1));
  snprintf(minute_text, sizeof(minute_text), "%02d", s % 60);
#else
  char *hour_format;
  if (clock_is_24h_style()) {
    hour_format = "%H";
  } else {
    hour_format = "%I";
  }
  strftime(hour_text, sizeof(hour_text), hour_format, tick_time);
  char *minute_format = "%M";
  strftime(minute_text, sizeof(minute_text), minute_format, tick_time);
#endif

  animation_schedule((Animation *)mario_animation_beg);
  animation_schedule((Animation *)block_animation_beg);
  
  int weather_age = time(NULL)-weather_last_update;
  if (units_changed & MINUTE_UNIT)
  {
    if (
         (config_show_phone_battery /* && ((phone_battery_level < 0) || (tick_time->tm_min % 30 == 0))*/)
         || (config_show_weather && (tick_time->tm_min % 5 == 0) && (weather_age > WEATHER_UPDATE_INTERVAL))
       )
      request_all();
  }
  if (config_show_weather && (weather_age > WEATHER_MAX_AGE) && (weather_icon_id >= 0))
  {
    load_weather_icon();
    layer_mark_dirty(phone_battery_layer);
  }
  
  if (units_changed & HOUR_UNIT)
  {
    need_update_background = 1;
    update_character(); // for 1st April
    if (config_vibe_hour)
    {
      static const uint32_t const segments[] = { 50, 100, 50, 100, 50, 100, 50, 100, 50 };
      VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
      };    
      vibes_enqueue_custom_pattern(pat);
    }
  }
}

int main(void)
{
  handle_init();
  app_event_loop();
  handle_deinit();
}
