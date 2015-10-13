// GB Enhanced Copyright Daniel Baxter 2013
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : lcd.cpp
// Date : August 16, 2014
// Description : Game Boy Advance LCD emulation
//
// Draws background, window, and sprites to screen
// Responsible for blitting pixel data and limiting frame rate

#include <cmath>

#include "lcd.h"

/****** LCD Constructor ******/
AGB_LCD::AGB_LCD()
{
	reset();
}

/****** LCD Destructor ******/
AGB_LCD::~AGB_LCD()
{
	screen_buffer.clear();
	scanline_buffer.clear();
	std::cout<<"LCD::Shutdown\n";
}

/****** Reset LCD ******/
void AGB_LCD::reset()
{
	final_screen = NULL;
	mem = NULL;

	scanline_buffer.clear();
	screen_buffer.clear();

	lcd_clock = 0;
	lcd_mode = 0;

	frame_start_time = 0;
	frame_current_time = 0;
	fps_count = 0;
	fps_time = 0;

	current_scanline = 0;
	scanline_pixel_counter = 0;

	screen_buffer.resize(0x9600, 0);
	scanline_buffer.resize(0x100, 0);

	//Initialize various LCD status variables
	lcd_stat.oam_update = false;
	lcd_stat.oam_update_list.resize(128, false);

	lcd_stat.bg_pal_update = true;
	lcd_stat.bg_pal_update_list.resize(256, true);

	lcd_stat.obj_pal_update = true;
	lcd_stat.obj_pal_update_list.resize(256, true);

	lcd_stat.frame_base = 0x6000000;
	lcd_stat.bg_mode = 0;
	lcd_stat.hblank_interval_free = false;
	lcd_stat.oam_access = false;

	lcd_stat.window_x1[0] = lcd_stat.window_x1[1] = 0;
	lcd_stat.window_y1[0] = lcd_stat.window_y1[1] = 0;
	lcd_stat.window_enable[0] = lcd_stat.window_enable[1] = false;

	lcd_stat.in_window = false;
	lcd_stat.obj_win_enable = false;
	lcd_stat.current_sfx_type = NORMAL;

	lcd_stat.bg_params[0].overflow = false;
	lcd_stat.bg_params[1].overflow = false;

	//BG Flip LUT generation
	for(int x = 0, y = 255; x < 255; x++, y--)
	{
		lcd_stat.bg_flip_lut[x] = (y % 8);
	}

	//BG Tile # LUT generation
	for(int y = 0; y < 256; y++)
	{
		for(int x = 0; x < 256; x++)
		{
			lcd_stat.bg_tile_lut[x][y] = ((y % 8) * 8) + (x % 8);
		}
	}

	//BG Tile Map# LUT generation
	for(int y = 0; y < 256; y++)
	{
		for(int x = 0; x < 256; x++)
		{
			lcd_stat.bg_num_lut[x][y] = ((y / 8) * 32) + (x / 8);
		}
	}


	for(int x = 0; x < 512; x++)
	{
		lcd_stat.screen_offset_lut[x] = (x > 255) ? 0x800 : 0x0;
	}

	for(int x = 0; x < 4; x++)
	{
		lcd_stat.bg_control[x] = 0;
		lcd_stat.bg_enable[x] = false;
		lcd_stat.bg_offset_x[x] = 0;
		lcd_stat.bg_offset_y[x] = 0;
		lcd_stat.bg_priority[x] = 0;
		lcd_stat.bg_depth[x] = 4;
		lcd_stat.bg_size[x] = 0;
		lcd_stat.bg_base_tile_addr[x] = 0x6000000;
		lcd_stat.bg_base_map_addr[x] = 0x6000000;
		lcd_stat.mode_0_width[x] = 256;
		lcd_stat.mode_0_height[x] = 256;
	}
}

/****** Initialize LCD with SDL ******/
bool AGB_LCD::init()
{
	if(config::sdl_render)
	{
		if(SDL_Init(SDL_INIT_EVERYTHING) == -1)
		{
			std::cout<<"LCD::Error - Could not initialize SDL\n";
			return false;
		}

		if(config::use_opengl) { opengl_init(); }
		else { final_screen = SDL_SetVideoMode(240, 160, 32, SDL_SWSURFACE); }

		if(final_screen == NULL) { return false; }
	}

	std::cout<<"LCD::Initialized\n";

	return true;
}

/****** Updates OAM entries when values in memory change ******/
void AGB_LCD::update_oam()
{
	lcd_stat.oam_update = false;
	
	u32 oam_ptr = 0x7000000;
	u16 attribute = 0;

	for(int x = 0; x < 128; x++)
	{
		//Update if OAM entry has changed
		if(lcd_stat.oam_update_list[x])
		{
			lcd_stat.oam_update_list[x] = false;

			//Read and parse Attribute 0
			attribute = mem->read_u16_fast(oam_ptr);
			oam_ptr += 2;

			obj[x].y = (attribute & 0xFF);
			obj[x].rotate_scale = (attribute & 0x100) ? 1 : 0;
			obj[x].type = (attribute & 0x200) ? 1 : 0;
			obj[x].mode = (attribute >> 10) & 0x3;
			obj[x].bit_depth = (attribute & 0x2000) ? 8 : 4;
			obj[x].shape = (attribute >> 14);

			if((obj[x].rotate_scale == 0) && (obj[x].type == 1)) { obj[x].visible = false; }
			else { obj[x].visible = true; }

			//Read and parse Attribute 1
			attribute = mem->read_u16_fast(oam_ptr);
			oam_ptr += 2;

			obj[x].x = (attribute & 0x1FF);
			obj[x].h_flip = (attribute & 0x1000) ? true : false;
			obj[x].v_flip = (attribute & 0x2000) ? true : false;
			obj[x].size = (attribute >> 14);

			//Read and parse Attribute 2
			attribute = mem->read_u16_fast(oam_ptr);
			oam_ptr += 4;

			obj[x].tile_number = (attribute & 0x3FF);
			obj[x].bg_priority = ((attribute >> 10) & 0x3);
			obj[x].palette_number = ((attribute >> 12) & 0xF); 

			//Determine dimensions of the sprite
			switch(obj[x].size)
			{
				//Size 0 - 8x8, 16x8, 8x16
				case 0x0:
					if(obj[x].shape == 0) { obj[x].width = 8; obj[x].height = 8; }
					else if(obj[x].shape == 1) { obj[x].width = 16; obj[x].height = 8; }
					else if(obj[x].shape == 2) { obj[x].width = 8; obj[x].height = 16; }
					break;

				//Size 1 - 16x16, 32x8, 8x32
				case 0x1:
					if(obj[x].shape == 0) { obj[x].width = 16; obj[x].height = 16; }
					else if(obj[x].shape == 1) { obj[x].width = 32; obj[x].height = 8; }
					else if(obj[x].shape == 2) { obj[x].width = 8; obj[x].height = 32; }
					break;

				//Size 2 - 32x32, 32x16, 16x32
				case 0x2:
					if(obj[x].shape == 0) { obj[x].width = 32; obj[x].height = 32; }
					else if(obj[x].shape == 1) { obj[x].width = 32; obj[x].height = 16; }
					else if(obj[x].shape == 2) { obj[x].width = 16; obj[x].height = 32; }
					break;

				//Size 3 - 64x64, 64x32, 32x64
				case 0x3:
					if(obj[x].shape == 0) { obj[x].width = 64; obj[x].height = 64; }
					else if(obj[x].shape == 1) { obj[x].width = 64; obj[x].height = 32; }
					else if(obj[x].shape == 2) { obj[x].width = 32; obj[x].height = 64; }
					break;
			}

			//Set double-size
			if((obj[x].rotate_scale == 1) && (obj[x].type == 1)) { obj[x].x += (obj[x].width >> 1); obj[x].y += (obj[x].height >> 1); }

			//Precalulate OBJ boundaries
			obj[x].left = obj[x].x;
			obj[x].right = (obj[x].x + obj[x].width - 1);

			obj[x].top = obj[x].y;
			obj[x].bottom = (obj[x].y + obj[x].height - 1);

			//Precalculate OBJ base address
			obj[x].addr = 0x6010000 + (obj[x].tile_number << 5);
		}

		else { oam_ptr += 8; }
	}

	//Update render list for the current scanline
	update_obj_render_list();
}

/****** Updates a list of OBJs to render on the current scanline ******/
void AGB_LCD::update_obj_render_list()
{
	obj_render_length = 0;

	//Cycle through all of the sprites
	for(int x = 0; x < 128; x++)
	{
		//Check to see if sprite is rendered on the current scanline
		if((obj[x].visible) && (current_scanline >= obj[x].top) && (current_scanline <= obj[x].bottom))
		{
			obj_render_list[obj_render_length++] = x;
		}
	}
}

/****** Updates palette entries when values in memory change ******/
void AGB_LCD::update_palettes()
{
	//Update BG palettes
	if(lcd_stat.bg_pal_update)
	{
		lcd_stat.bg_pal_update = false;

		//Cycle through all updates to BG palettes
		for(int x = 0; x < 256; x++)
		{
			//If this palette has been updated, convert to ARGB
			if(lcd_stat.bg_pal_update_list[x])
			{
				lcd_stat.bg_pal_update_list[x] = false;

				u16 color_bytes = mem->read_u16_fast(0x5000000 + (x << 1));
				raw_pal[x][0] = color_bytes;

				u8 red = ((color_bytes & 0x1F) << 3);
				color_bytes >>= 5;

				u8 green = ((color_bytes & 0x1F) << 3);
				color_bytes >>= 5;

				u8 blue = ((color_bytes & 0x1F) << 3);

				pal[x][0] =  0xFF000000 | (red << 16) | (green << 8) | (blue);
			}
		}
	}

	//Update OBJ palettes
	if(lcd_stat.obj_pal_update)
	{
		lcd_stat.obj_pal_update = false;

		//Cycle through all updates to OBJ palettes
		for(int x = 0; x < 256; x++)
		{
			//If this palette has been updated, convert to ARGB
			if(lcd_stat.obj_pal_update_list[x])
			{
				lcd_stat.obj_pal_update_list[x] = false;

				u16 color_bytes = mem->read_u16_fast(0x5000200 + (x << 1));
				raw_pal[x][1] = color_bytes;

				u8 red = ((color_bytes & 0x1F) << 3);
				color_bytes >>= 5;

				u8 green = ((color_bytes & 0x1F) << 3);
				color_bytes >>= 5;

				u8 blue = ((color_bytes & 0x1F) << 3);

				pal[x][1] =  0xFF000000 | (red << 16) | (green << 8) | (blue);
			}
		}
	}
}

/****** Determines if a sprite pixel should be rendered, and if so draws it to the current scanline pixel ******/
bool AGB_LCD::render_sprite_pixel()
{
	//If sprites are disabled, quit now
	if((lcd_stat.display_control & 0x1000) == 0) { return false; }

	//If no sprites are rendered on this line, quit now
	if(obj_render_length == 0) { return false; }

	u8 sprite_id = 0;
	u32 sprite_tile_addr = 0;
	u32 meta_sprite_tile = 0;
	u8 raw_color = 0;

	//Cycle through all sprites that are rendering on this pixel, draw them according to their priority
	for(int x = 0; x < obj_render_length; x++)
	{
		sprite_id = obj_render_list[x];

		//Check to see if current_scanline_pixel is within sprite
		if((scanline_pixel_counter >= obj[sprite_id].left) && (scanline_pixel_counter <= obj[sprite_id].right)) 
		{
			//Determine the internal X-Y coordinates of the sprite's pixel
			u16 sprite_tile_pixel_x = scanline_pixel_counter - obj[sprite_id].x;
			u16 sprite_tile_pixel_y = current_scanline - obj[sprite_id].y;

			//Horizontal flip the internal X coordinate
			if(obj[sprite_id].h_flip)
			{
				s16 h_flip = sprite_tile_pixel_x;
				h_flip -= (obj[sprite_id].width - 1);

				if(h_flip < 0) { h_flip *= -1; }

				sprite_tile_pixel_x = h_flip;
			}

			//Vertical flip the internal Y coordinate
			if(obj[sprite_id].v_flip)
			{
				s16 v_flip = sprite_tile_pixel_y;
				v_flip -= (obj[sprite_id].height - 1);

				if(v_flip < 0) { v_flip *= -1; }

				sprite_tile_pixel_y = v_flip;
			}

			//Determine meta x-coordinate of rendered sprite pixel
			u8 meta_x = (sprite_tile_pixel_x / 8);

			//Determine meta Y-coordinate of rendered sprite pixel
			u8 meta_y = (sprite_tile_pixel_y / 8);

			//Determine which 8x8 section to draw pixel from, and what tile that actually represents in VRAM
			if(lcd_stat.display_control & 0x40)
			{
				meta_sprite_tile = (meta_y * (obj[sprite_id].width/8)) + meta_x;	
			}

			else
			{
				meta_sprite_tile = (meta_y * 32) + meta_x;
			}

			sprite_tile_addr = obj[sprite_id].addr + (meta_sprite_tile * (obj[sprite_id].bit_depth << 3));

			meta_x = (sprite_tile_pixel_x % 8);
			meta_y = (sprite_tile_pixel_y % 8);

			u8 sprite_tile_pixel = (meta_y * 8) + meta_x;

			//Grab the byte corresponding to (sprite_tile_pixel), render it as ARGB - 4-bit version
			if(obj[sprite_id].bit_depth == 4)
			{
				sprite_tile_addr += (sprite_tile_pixel >> 1);
				raw_color = mem->memory_map[sprite_tile_addr];

				if((sprite_tile_pixel % 2) == 0) { raw_color &= 0xF; }
				else { raw_color >>= 4; }

				if(raw_color != 0) 
				{
					//If this sprite is in OBJ Window mode, do not render it, but set a flag indicating the LCD passed over its pixel
					if(obj[sprite_id].mode == 2) { obj_win_pixel = true; }

					else 
					{
						scanline_buffer[scanline_pixel_counter] = pal[((obj[sprite_id].palette_number * 32) + (raw_color * 2)) >> 1][1];
						last_raw_color = raw_pal[((obj[sprite_id].palette_number * 32) + (raw_color * 2)) >> 1][1];
						last_obj_priority = obj[sprite_id].bg_priority;
						last_obj_mode = obj[sprite_id].mode;
						return true;
					}
				}
			}

			//Grab the byte corresponding to (sprite_tile_pixel), render it as ARGB - 8-bit version
			else
			{
				sprite_tile_addr += sprite_tile_pixel;
				raw_color = mem->memory_map[sprite_tile_addr];

				if(raw_color != 0) 
				{
					//If this sprite is in OBJ Window mode, do not render it, but set a flag indicating the LCD passed over its pixel
					if(obj[sprite_id].mode == 2) { obj_win_pixel = true; }

					else
					{
						scanline_buffer[scanline_pixel_counter] = pal[raw_color][1];
						last_raw_color = raw_pal[raw_color][1];
						last_obj_priority = obj[sprite_id].bg_priority;
						last_obj_mode = obj[sprite_id].mode;
						return true;
					}
				}
			}
		}
	}

	//Return false if nothing was drawn
	return false;
}

/****** Determines if a background pixel should be rendered, and if so draws it to the current scanline pixel ******/
bool AGB_LCD::render_bg_pixel(u32 bg_control)
{
	if((!lcd_stat.bg_enable[0]) && (bg_control == BG0CNT)) { return false; }
	else if((!lcd_stat.bg_enable[1]) && (bg_control == BG1CNT)) { return false; }
	else if((!lcd_stat.bg_enable[2]) && (bg_control == BG2CNT)) { return false; }
	else if((!lcd_stat.bg_enable[3]) && (bg_control == BG3CNT)) { return false; }

	//Render BG pixel according to current BG Mode
	switch(lcd_stat.bg_mode)
	{
		//BG Mode 0
		case 0:
			return render_bg_mode_0(bg_control); break;

		//BG Mode 1
		case 1:
			//Render BG2 as Scaled+Rotation
			if(bg_control == BG2CNT) { return render_bg_mode_1(bg_control); }

			//BG3 is never drawn in Mode 1
			else if(bg_control == BG3CNT) { return false; }

			//Render BG0 and BG1 as Text (same as Mode 0)
			else { return render_bg_mode_0(bg_control); } 

			break;

		//BG Mode 2
		case 0x2:
			//Render BG2 and BG3 as Scaled+Rotation
			if((bg_control == BG2CNT) || (bg_control == BG3CNT)) { return render_bg_mode_1(bg_control); }
			else { return false; }

			break;

		//BG Mode 3
		case 3:
			return render_bg_mode_3(); break;

		//BG Mode 4
		case 4:
			return render_bg_mode_4(); break;

		//BG Mode 5
		case 5:
			return render_bg_mode_5(); break;

		default:
			//std::cout<<"LCD::invalid or unsupported BG Mode : " << std::dec << (lcd_stat.display_control & 0x7);
			return false;
	}
}

/****** Render BG Mode 0 ******/
bool AGB_LCD::render_bg_mode_0(u32 bg_control)
{
	//Set BG ID to determine which BG is being rendered.
	u8 bg_id = (bg_control - 0x4000008) >> 1;
	
	//BG offset
	u16 screen_offset = 0;

	//Determine meta x-coordinate of rendered BG pixel
	u16 meta_x = ((scanline_pixel_counter + lcd_stat.bg_offset_x[bg_id]) % lcd_stat.mode_0_width[bg_id]);

	//Determine meta Y-coordinate of rendered BG pixel
	u16 meta_y = ((current_scanline + lcd_stat.bg_offset_y[bg_id]) % lcd_stat.mode_0_height[bg_id]);
	
	//Determine the address offset for the screen
	switch(lcd_stat.bg_size[bg_id])
	{
		//Size 0 - 256x256
		case 0x0: break;

		//Size 1 - 512x256
		case 0x1: 
			screen_offset = lcd_stat.screen_offset_lut[meta_x];
			break;

		//Size 2 - 256x512
		case 0x2:
			screen_offset = lcd_stat.screen_offset_lut[meta_y];
			break;

		//Size 3 - 512x512
		case 0x3:
			screen_offset = (meta_y > 255) ? (lcd_stat.screen_offset_lut[meta_x] | 0x1000) : lcd_stat.screen_offset_lut[meta_x];
			break;
	}

	//Add screen offset to current BG map base address
	u32 map_base_addr = lcd_stat.bg_base_map_addr[bg_id] + screen_offset;

	//Determine the X-Y coordinates of the BG's tile on the tile map
	u16 current_tile_pixel_x = ((scanline_pixel_counter + lcd_stat.bg_offset_x[bg_id]) % 256);
	u16 current_tile_pixel_y = ((current_scanline + lcd_stat.bg_offset_y[bg_id]) % 256);

	//Get current map entry for rendered pixel
	u16 tile_number = lcd_stat.bg_num_lut[current_tile_pixel_x][current_tile_pixel_y];

	//Grab the map's data
	u16 map_data = mem->read_u16_fast(map_base_addr + (tile_number * 2));

	//Look at the Tile Map #(tile_number), see what Tile # it points to
	u16 map_entry = map_data & 0x3FF;

	//Grab horizontal and vertical flipping options
	u8 flip_options = (map_data >> 10) & 0x3;

	//Grab the Palette number of the tiles
	u8 palette_number = (map_data >> 12);

	//Get address of Tile #(map_entry)
	u32 tile_addr = lcd_stat.bg_base_tile_addr[bg_id] + (map_entry * (lcd_stat.bg_depth[bg_id] << 3));

	switch(flip_options)
	{
		case 0x0: break;

		//Horizontal flip
		case 0x1: 
			current_tile_pixel_x = lcd_stat.bg_flip_lut[current_tile_pixel_x];
			break;

		//Vertical flip
		case 0x2:
			current_tile_pixel_y = lcd_stat.bg_flip_lut[current_tile_pixel_y];
			break;

		//Horizontal + vertical flip
		case 0x3:
			current_tile_pixel_x = lcd_stat.bg_flip_lut[current_tile_pixel_x];
			current_tile_pixel_y = lcd_stat.bg_flip_lut[current_tile_pixel_y];
			break;
	}

	u8 current_tile_pixel = lcd_stat.bg_tile_lut[current_tile_pixel_x][current_tile_pixel_y];

	//Grab the byte corresponding to (current_tile_pixel), render it as ARGB - 4-bit version
	if(lcd_stat.bg_depth[bg_id] == 4)
	{
		tile_addr += (current_tile_pixel >> 1);
		u8 raw_color = mem->memory_map[tile_addr];

		if((current_tile_pixel % 2) == 0) { raw_color &= 0xF; }
		else { raw_color >>= 4; }

		//If the bg color is transparent, abort drawing
		if(raw_color == 0) { return false; }

		scanline_buffer[scanline_pixel_counter] = pal[((palette_number * 32) + (raw_color * 2)) >> 1][0];
		last_raw_color = raw_pal[((palette_number * 32) + (raw_color * 2)) >> 1][0];
	}

	//Grab the byte corresponding to (current_tile_pixel), render it as ARGB - 8-bit version
	else
	{
		tile_addr += current_tile_pixel;
		u8 raw_color = mem->memory_map[tile_addr];

		//If the bg color is transparent, abort drawing
		if(raw_color == 0) { return false; }

		scanline_buffer[scanline_pixel_counter] = pal[raw_color][0];
		last_raw_color = raw_pal[raw_color][0];
	}

	return true;
}

/****** Render BG Mode 1 ******/
bool AGB_LCD::render_bg_mode_1(u32 bg_control)
{
	//Set BG ID to determine which BG is being rendered.
	u8 bg_id = (bg_control - 0x4000008) >> 1;
	u8 scale_rot_id = (bg_id == 2) ? 0 : 1;

	//Get BG size in tiles, pixels
	//0 - 128x128, 1 - 256x256, 2 - 512x512, 3 - 1024x1024
	u16 bg_tile_size = (16 << (lcd_stat.bg_control[bg_id] >> 14));
	u16 bg_pixel_size = bg_tile_size << 3;

	//Calculate new X-Y coordinates from scaling+rotation
	double new_x = lcd_stat.bg_params[scale_rot_id].x_ref + (lcd_stat.bg_params[scale_rot_id].a * scanline_pixel_counter) + (lcd_stat.bg_params[scale_rot_id].b * current_scanline);
	double new_y = lcd_stat.bg_params[scale_rot_id].y_ref + (lcd_stat.bg_params[scale_rot_id].c * scanline_pixel_counter) + (lcd_stat.bg_params[scale_rot_id].d * current_scanline);

	//Round results to nearest integer
	new_x = (new_x > 0) ? floor(new_x + 0.5) : ceil(new_x - 0.5);
	new_y = (new_y > 0) ? floor(new_y + 0.5) : ceil(new_y - 0.5);

	//Clip BG if coordinates overflow and overflow flag is not set
	if(!lcd_stat.bg_params[scale_rot_id].overflow)
	{
		if((new_x >= bg_pixel_size) || (new_x < 0)) { return false; }
		if((new_y >= bg_pixel_size) || (new_y < 0)) { return false; }
	}

	//Wrap BG if coordinates overflow and overflow flag is set
	else 
	{
		while(new_x >= bg_pixel_size) { new_x -= bg_pixel_size; }
		while(new_y >= bg_pixel_size) { new_y -= bg_pixel_size; }
		while(new_x < 0) { new_x += bg_pixel_size; }
		while(new_y < 0) { new_y += bg_pixel_size; }
	}

	//Determine source pixel X-Y coordinates
	u16 src_x = new_x; 
	u16 src_y = new_y;

	//Get current map entry for rendered pixel
	u16 tile_number = ((src_y / 8) * bg_tile_size) + (src_x / 8);

	//Look at the Tile Map #(tile_number), see what Tile # it points to
	u16 map_entry = mem->read_u16_fast(lcd_stat.bg_base_map_addr[bg_id] + tile_number) & 0xFF;

	//Get address of Tile #(map_entry)
	u32 tile_addr = lcd_stat.bg_base_tile_addr[bg_id] + (map_entry * 64);

	u8 current_tile_pixel = ((src_y % 8) * 8) + (src_x % 8);

	//Grab the byte corresponding to (current_tile_pixel), render it as ARGB - 8-bit version
	tile_addr += current_tile_pixel;
	u8 raw_color = mem->memory_map[tile_addr];

	//If the bg color is transparent, abort drawing
	if(raw_color == 0) { return false; }

	scanline_buffer[scanline_pixel_counter] = pal[raw_color][0];
	last_raw_color = raw_pal[raw_color][0];

	return true;
}

/****** Render BG Mode 3 ******/
bool AGB_LCD::render_bg_mode_3()
{
	//Determine which byte in VRAM to read for color data
	u16 color_bytes = mem->read_u16_fast(0x6000000 + (current_scanline * 480) + (scanline_pixel_counter * 2));
	last_raw_color = color_bytes;

	//ARGB conversion
	u8 red = ((color_bytes & 0x1F) * 8);
	color_bytes >>= 5;

	u8 green = ((color_bytes & 0x1F) * 8);
	color_bytes >>= 5;

	u8 blue = ((color_bytes & 0x1F) * 8);

	scanline_buffer[scanline_pixel_counter] = 0xFF000000 | (red << 16) | (green << 8) | (blue);

	return true;
}

/****** Render BG Mode 4 ******/
bool AGB_LCD::render_bg_mode_4()
{
	//Determine which byte in VRAM to read for color data
	u32 bitmap_entry = (lcd_stat.frame_base + (current_scanline * 240) + scanline_pixel_counter);

	u8 raw_color = mem->memory_map[bitmap_entry];
	if(raw_color == 0) { return false; }

	scanline_buffer[scanline_pixel_counter] = pal[raw_color][0];
	last_raw_color = raw_pal[raw_color][0];

	return true;
}

/****** Render BG Mode 5 ******/
bool AGB_LCD::render_bg_mode_5()
{
	//Restrict rendering to 160x128
	if(scanline_pixel_counter >= 160) { return false; }
	if(current_scanline >= 128) { return false; }

	//Determine which byte in VRAM to read for color data
	u16 color_bytes = mem->read_u16_fast(lcd_stat.frame_base + (current_scanline * 320) + (scanline_pixel_counter * 2));
	last_raw_color = color_bytes;

	//ARGB conversion
	u8 red = ((color_bytes & 0x1F) * 8);
	color_bytes >>= 5;

	u8 green = ((color_bytes & 0x1F) * 8);
	color_bytes >>= 5;

	u8 blue = ((color_bytes & 0x1F) * 8);

	scanline_buffer[scanline_pixel_counter] = 0xFF000000 | (red << 16) | (green << 8) | (blue);

	return true;
}

/****** Render pixels for a given scanline (per-pixel) ******/
void AGB_LCD::render_scanline()
{
	bool obj_render = false;
	lcd_stat.in_window = false;
	lcd_stat.current_window = 0;
	last_obj_priority = 0xFF;
	last_bg_priority = 0x5;
	last_obj_mode = 0;
	last_raw_color = raw_pal[0][0];
	obj_win_pixel = false;
	u8 bg_render_list[4];
	u8 bg_id;

	//Render sprites
	obj_render = render_sprite_pixel();

	//Determine window status of this pixel
	if(lcd_stat.window_enable[0])
	{
		if((scanline_pixel_counter < lcd_stat.window_x1[0]) || (scanline_pixel_counter > lcd_stat.window_x2[0])
		|| (current_scanline < lcd_stat.window_y1[0]) || (current_scanline > lcd_stat.window_y2[0]))
		{
			lcd_stat.in_window = false;
		}
		
		else { lcd_stat.in_window = true; lcd_stat.current_window = 0; }
	}

	if((lcd_stat.window_enable[1]) && (!lcd_stat.in_window))
	{
		if(!lcd_stat.window_enable[0]) { lcd_stat.current_window = 1; }

		if((scanline_pixel_counter < lcd_stat.window_x1[1]) || (scanline_pixel_counter > lcd_stat.window_x2[1])
		|| (current_scanline < lcd_stat.window_y1[1]) || (current_scanline > lcd_stat.window_y2[1]))
		{
			lcd_stat.in_window = false;
		}
		
		else { lcd_stat.in_window = true; lcd_stat.current_window = 1; }
	}

	//Turn off OBJ rendering if in/out of a window where OBJ rendering is disabled
	if((lcd_stat.window_enable[lcd_stat.current_window]) && (!lcd_stat.in_window) && (!lcd_stat.window_out_enable[4][0])) { obj_render = false; }
	else if((lcd_stat.window_enable[lcd_stat.current_window]) && (lcd_stat.in_window) && (!lcd_stat.window_in_enable[4][lcd_stat.current_window])) { obj_render = false; }

	//Determine BG rendering priority
	for(int x = 0, list_length = 0; x < 4; x++)
	{
		if(lcd_stat.bg_priority[0] == x) { bg_render_list[list_length++] = 0; }
		if(lcd_stat.bg_priority[1] == x) { bg_render_list[list_length++] = 1; }
		if(lcd_stat.bg_priority[2] == x) { bg_render_list[list_length++] = 2; }
		if(lcd_stat.bg_priority[3] == x) { bg_render_list[list_length++] = 3; }
	}

	//Render BGs based on priority (3 is the 'lowest', 0 is the 'highest')
	for(int x = 0; x < 4; x++)
	{
		bg_id = bg_render_list[x];

		if((obj_render) && (last_obj_priority <= lcd_stat.bg_priority[bg_id])) { last_bg_priority = 4; return; }
		else if((lcd_stat.window_enable[lcd_stat.current_window]) && (!lcd_stat.in_window) && (!lcd_stat.window_out_enable[bg_id][0])) { continue; }
		else if((lcd_stat.window_enable[lcd_stat.current_window]) && (lcd_stat.in_window) && (!lcd_stat.window_in_enable[bg_id][lcd_stat.current_window])) { continue; }
		else if((lcd_stat.obj_win_enable) && (!obj_win_pixel) && (!lcd_stat.window_out_enable[bg_id][0])) { continue; }
		else if(render_bg_pixel(BG0CNT + (bg_id << 1))) { last_bg_priority = bg_id; return; }
	}

	//Use BG Palette #0, Color #0 as the backdrop
	scanline_buffer[scanline_pixel_counter] = pal[0][0];
}

/****** Applies the GBA's SFX to a pixel ******/
void AGB_LCD::apply_sfx()
{
	lcd_stat.temp_sfx_type = lcd_stat.current_sfx_type;

	//If doing brightness up/down and the last pixel drawn is not a target, abort SFX
	if((!lcd_stat.sfx_target[last_bg_priority][0]) && (lcd_stat.current_sfx_type != ALPHA_BLEND)) { return; }

	//If doing brightness up/down and the OBJ mode is Semi-Transparent, force alpha blending
	if((last_bg_priority == 4) && (lcd_stat.current_sfx_type != ALPHA_BLEND) && (last_obj_mode == 1)) { lcd_stat.current_sfx_type = ALPHA_BLEND;  }

	//If doing alpha blending outside of the OBJ Window and the last pixel drawn is not a target, abort SFX 
	if((!lcd_stat.sfx_target[last_bg_priority][0]) && (lcd_stat.current_sfx_type == ALPHA_BLEND) && (!obj_win_pixel) && (last_obj_mode != 1)) { return; }

	bool do_sfx = false;

	//Apply SFX if in Window
	if((lcd_stat.window_enable[lcd_stat.current_window]) && (lcd_stat.in_window) && (lcd_stat.window_in_enable[5][lcd_stat.current_window])) { do_sfx = true; }

	//Apply SFX if out of Window
	else if((lcd_stat.window_enable[lcd_stat.current_window]) && (!lcd_stat.in_window) && (lcd_stat.window_out_enable[5][0])) { do_sfx = true; }

	//Apply SFX if in OBJ Window
	else if((lcd_stat.obj_win_enable) && (obj_win_pixel) && (lcd_stat.window_out_enable[5][1])) { do_sfx = true; }

	//Apply SFX to whole screen
	else if((!lcd_stat.window_enable[0]) && (!lcd_stat.window_enable[1]) && (!lcd_stat.obj_win_enable)) { do_sfx = true; }

	//Apply SFX (alpha-blending) if OBJ is semi-transparent
	else if(last_obj_mode == 1) { do_sfx = true; }

	if(!do_sfx) { return; }

	//Apply the specified SFX
	switch(lcd_stat.current_sfx_type)
	{
		case ALPHA_BLEND:
			scanline_buffer[scanline_pixel_counter] = alpha_blend();
			break;

		case BRIGHTNESS_UP: 
			scanline_buffer[scanline_pixel_counter] = brightness_up(); 
			break;

		case BRIGHTNESS_DOWN:
			scanline_buffer[scanline_pixel_counter] = brightness_down(); 
			break;
	}

	lcd_stat.current_sfx_type = lcd_stat.temp_sfx_type;
	lcd_stat.temp_sfx_type = NORMAL;
}

/****** SFX - Increase brightness ******/
u32 AGB_LCD::brightness_up()
{
	u16 color_bytes = last_raw_color;
	u16 result = 0;

	//Increase RGB intensities
	u8 red = (color_bytes & 0x1F);
	result = red + ((0x1F - red) * lcd_stat.brightness_coef);
	red = (result > 0x1F) ? 0x1F : result;
	color_bytes >>= 5;

	u8 green = (color_bytes & 0x1F);
	result = green + ((0x1F - green) * lcd_stat.brightness_coef);
	green = (result > 0x1F) ? 0x1F : result;
	color_bytes >>= 5;

	u8 blue = (color_bytes & 0x1F);
	result = blue + ((0x1F - blue) * lcd_stat.brightness_coef);
	blue = (result > 0x1F) ? 0x1F : result;

	//Return 32-bit color
	return 0xFF000000 | (red << 19) | (green << 11) | (blue << 3);
}

/****** SFX - Decrease brightness ******/
u32 AGB_LCD::brightness_down()
{
	u16 color_bytes = last_raw_color;

	//Decrease RGB intensities 
	u8 red = (color_bytes & 0x1F);
	red -= (red * lcd_stat.brightness_coef);
	color_bytes >>= 5;

	u8 green = (color_bytes & 0x1F);
	green -= (green * lcd_stat.brightness_coef);
	color_bytes >>= 5;

	u8 blue = (color_bytes & 0x1F);
	blue -= (blue * lcd_stat.brightness_coef);

	//Return 32-bit color
	return 0xFF000000 | (red << 19) | (green << 11) | (blue << 3);
}

/****** SFX - Alpha blending ******/
u32 AGB_LCD::alpha_blend()
{		
	//Grab the current 32-bit color for this pixel, in case no alpha blending occurs
	u32 final_color = scanline_buffer[scanline_pixel_counter];

	u16 color_1 = last_raw_color;
	u16 color_2 = 0x0;
	u16 result = 0;
	u8 next_bg_priority = 0;
	bool do_blending = false;

	//When blending with an OBJ Window pixel, the 1st target needs to be manually calculated
	if(obj_win_pixel)
	{
		for(int x = 0; x < 4; x++)
		{
			//OBJ is 1st target
			if((last_obj_priority == x) && (lcd_stat.sfx_target[4][0]) && (!do_blending)) { do_blending = render_sprite_pixel(); last_bg_priority = 4;  }
	
			//BG0 is 1st target
			if((lcd_stat.bg_priority[0] == x) && (lcd_stat.sfx_target[0][0]) && (!do_blending)) { do_blending = render_bg_pixel(BG0CNT); last_bg_priority = 0; }

			//BG1 is 1st target
			if((lcd_stat.bg_priority[1] == x) && (lcd_stat.sfx_target[1][0]) && (!do_blending)) { do_blending = render_bg_pixel(BG1CNT); last_bg_priority = 1; }

			//BG2 is 1st target
			if((lcd_stat.bg_priority[2] == x) && (lcd_stat.sfx_target[2][0]) && (!do_blending)) { do_blending = render_bg_pixel(BG2CNT); last_bg_priority = 2; }

			//BG3 is 1st target
			if((lcd_stat.bg_priority[3] == x) && (lcd_stat.sfx_target[3][0]) && (!do_blending)) { do_blending = render_bg_pixel(BG3CNT); last_bg_priority = 3; }

			if(do_blending) { x = 4; }
		}

		do_blending = false;
		color_1 = last_raw_color;
	}

	//If the BD is the 1st target, abort alpha blending (no pixel technically exists behind it for blending)
	if(last_bg_priority == 5) { return final_color; }

	//If BG0-3 was drawn last but is not the 1st target, abort alpha blending
	if((last_bg_priority < 4) && (!lcd_stat.sfx_target[last_bg_priority][0])) { return final_color; }

	//Determine which priority to start looking at to grab the 2nd target
	u8 current_bg_priority = (last_bg_priority == 4) ? last_obj_priority : lcd_stat.bg_priority[last_bg_priority];

	//Grab next closest 2nd target, if any
	for(int x = current_bg_priority; x < 4; x++)
	{
		//Blend with OBJ
		if((last_obj_priority == x) && (last_bg_priority != 4) && (!do_blending)) { do_blending = render_sprite_pixel(); next_bg_priority = 4; }
	
		//Blend with BG0
		if((lcd_stat.bg_priority[0] == x) && (last_bg_priority != 0) && (!do_blending)) { do_blending = render_bg_pixel(BG0CNT); next_bg_priority = 0; }

		//Blend with BG1
		if((lcd_stat.bg_priority[1] == x) && (last_bg_priority != 1) && (!do_blending)) { do_blending = render_bg_pixel(BG1CNT); next_bg_priority = 1; }

		//Blend with BG2
		if((lcd_stat.bg_priority[2] == x) && (last_bg_priority != 2) && (!do_blending)) { do_blending = render_bg_pixel(BG2CNT); next_bg_priority = 2; }

		//Blend with BG3
		if((lcd_stat.bg_priority[3] == x) && (last_bg_priority != 3) && (!do_blending)) { do_blending = render_bg_pixel(BG3CNT); next_bg_priority = 3; }

		if(do_blending) { x = 4; }
	}

	//Grab BD as 2nd target if possible
	if((!do_blending) && (lcd_stat.sfx_target[5][1])) { last_raw_color = raw_pal[0][0]; do_blending = true; }

	//If the 2nd target is rendered and not specified for blending, abort 
	if((do_blending) && (!lcd_stat.sfx_target[next_bg_priority][1])) { return final_color; } 

	if(!do_blending) 
	{
		//If no alpha-blending occurs, see if Brightness Increase can be applied (for semi-transparent OBJ only)
		if(lcd_stat.temp_sfx_type == BRIGHTNESS_UP) { return brightness_up(); }

		//If no alpha-blending occurs, see if Brightness Decrease can be applied (for semi-transparent OBJ only)
		else if(lcd_stat.temp_sfx_type == BRIGHTNESS_DOWN) { return brightness_down(); }

		//If no alpha-blending occurs and no fringe cases occur, abort
		else { return final_color; }
	}

	color_2 = last_raw_color;

	//Alpha-blending
	result = ((color_1 & 0x1F) * lcd_stat.alpha_a_coef) + ((color_2 & 0x1F) * lcd_stat.alpha_b_coef);
	u8 red = (result > 0x1F) ? 0x1F : result;
	color_1 >>= 5;
	color_2 >>= 5;

	result = ((color_1 & 0x1F) * lcd_stat.alpha_a_coef) + ((color_2 & 0x1F) * lcd_stat.alpha_b_coef);
	u8 green = (result > 0x1F) ? 0x1F : result;
	color_1 >>= 5;
	color_2 >>= 5;

	result = ((color_1 & 0x1F) * lcd_stat.alpha_a_coef) + ((color_2 & 0x1F) * lcd_stat.alpha_b_coef);
	u8 blue = (result > 0x1F) ? 0x1F : result;
	color_1 >>= 5;
	color_2 >>= 5;

	//Return 32-bit color
	return 0xFF000000 | (red << 19) | (green << 11) | (blue << 3);
}
	
/****** Run LCD for one cycle ******/
void AGB_LCD::step()
{
	lcd_clock++;

	//Mode 0 - Scanline rendering
	if(((lcd_clock % 1232) <= 960) && (lcd_clock < 197120)) 
	{
		//Increment scanline count
		if(mem->memory_map[DISPSTAT] & 0x2)
		{
			current_scanline++;
			mem->write_u16_fast(VCOUNT, current_scanline);
			scanline_compare();
		}

		//Disable OAM access
		lcd_stat.oam_access = false;

		//Change mode
		if(lcd_mode != 0) 
		{
			//Toggle HBlank flag OFF
			mem->memory_map[DISPSTAT] &= ~0x2;

			lcd_mode = 0; 
			update_obj_render_list();
		}

		//Render scanline data (per-pixel every 4 cycles)
		if((lcd_clock % 4) == 0) 
		{
			//Update OAM
			if(lcd_stat.oam_update) { update_oam(); }

			//Update palettes
			if((lcd_stat.bg_pal_update) || (lcd_stat.obj_pal_update)) { update_palettes(); }

			render_scanline();
			if(lcd_stat.current_sfx_type != NORMAL) { apply_sfx(); }
			scanline_pixel_counter++;
		}
	}

	//Mode 1 - H-Blank
	else if(((lcd_clock % 1232) > 960) && (lcd_clock < 197120))
	{
		//Permit OAM access if HBlank Interval Free flag is set
		if(lcd_stat.hblank_interval_free) { lcd_stat.oam_access = true; }

		//Change mode
		if(lcd_mode != 1) 
		{
			//Toggle HBlank flag ON
			mem->memory_map[DISPSTAT] |= 0x2;

			lcd_mode = 1;
			scanline_pixel_counter = 0;

			//Raise HBlank interrupt
			if(mem->memory_map[DISPSTAT] & 0x10) { mem->memory_map[REG_IF] |= 0x2; }

			//Push scanline data to final buffer - Only if Forced Blank is disabled
			if((lcd_stat.display_control & 0x80) == 0)
			{
				for(int x = 0, y = (240 * current_scanline); x < 240; x++, y++)
				{
					screen_buffer[y] = scanline_buffer[x];
				}
			}

			//Draw all-white during Forced Blank
			else
			{
				for(int x = 0, y = (240 * current_scanline); x < 240; x++, y++)
				{
					screen_buffer[y] = 0xFFFFFFFF;
				}
			}
	
			//Start HBlank DMA
			mem->start_blank_dma();
		}
	}

	//Mode 2 - VBlank
	else
	{
		//Toggle VBlank flag
		if(current_scanline < 227 ) { mem->memory_map[DISPSTAT] |= 0x1; }
		else { mem->memory_map[DISPSTAT] &= ~0x1; }

		//Permit OAM write access
		lcd_stat.oam_access = true;

		//Change mode
		if(lcd_mode != 2) 
		{
			lcd_mode = 2;

			//Toggle HBlank flag OFF
			mem->memory_map[DISPSTAT] &= ~0x2;

			//Increment scanline count
			current_scanline++;
			mem->write_u16_fast(VCOUNT, current_scanline);
			scanline_compare();

			//Raise VBlank interrupt
			if(mem->memory_map[DISPSTAT] & 0x8) { mem->memory_map[REG_IF] |= 0x1; }

			//Render final buffer - Only if Forced Blank is disabled
			if((mem->memory_map[DISPCNT] & 0x80) == 0)
			{
				//Use SDL
				if(config::sdl_render)
				{
					//Lock source surface
					if(SDL_MUSTLOCK(final_screen)){ SDL_LockSurface(final_screen); }
					u32* out_pixel_data = (u32*)final_screen->pixels;

					for(int a = 0; a < 0x9600; a++) { out_pixel_data[a] = screen_buffer[a]; }

					//Unlock source surface
					if(SDL_MUSTLOCK(final_screen)){ SDL_UnlockSurface(final_screen); }
		
					//Display final screen buffer - OpenGL
					if(config::use_opengl) { opengl_blit(); }
				
					//Display final screen buffer - SDL
					else 
					{
						if(SDL_Flip(final_screen) == -1) { std::cout<<"LCD::Error - Could not blit\n"; }
					}
				}

				//Use external rendering method (GUI)
				else { config::render_external(screen_buffer); }
			}

			//Limit framerate
			if(!config::turbo)
			{
				frame_current_time = SDL_GetTicks();
				if((frame_current_time - frame_start_time) < 16) { SDL_Delay(16 - (frame_current_time - frame_start_time));}
				frame_start_time = SDL_GetTicks();
			}

			//Update FPS counter + title
			fps_count++;
			if(((SDL_GetTicks() - fps_time) >= 1000) && (config::sdl_render))
			{ 
				fps_time = SDL_GetTicks(); 
				config::title.str("");
				config::title << "GBE+ " << fps_count << "FPS";
				SDL_WM_SetCaption(config::title.str().c_str(), NULL);
				fps_count = 0; 
			}
		}

		//Setup HBlank
		else if((lcd_clock % 1232) == 960) 
		{
			//Toggle HBlank flag ON
			mem->memory_map[DISPSTAT] |= 0x2;

			//Raise HBlank interrupt
			if(mem->memory_map[DISPSTAT] & 0x10) { mem->memory_map[REG_IF] |= 0x2; }
		}

		//Reset LCD clock
		else if(lcd_clock == 280896) 
		{
			//Toggle VBlank flag OFF
			mem->memory_map[DISPSTAT] &= ~0x1;

			//Toggle HBlank flag OFF
			mem->memory_map[DISPSTAT] &= ~0x2;

			lcd_clock = 0; 
			current_scanline = 0; 
			mem->write_u16_fast(VCOUNT, 0);
			scanline_compare();
			scanline_pixel_counter = 0; 
		}

		//Increment Scanline after HBlank
		else if(lcd_clock % 1232 == 0)
		{
			//Toggle HBlank flag OFF
			mem->memory_map[DISPSTAT] &= ~0x2;

			current_scanline++;
			mem->write_u16_fast(VCOUNT, current_scanline);
			scanline_compare();
		}
	}

	if(mem->memory_map[DISPCNT] & 0x80) { lcd_stat.oam_access = true; }
}

/****** Compare VCOUNT to LYC ******/
void AGB_LCD::scanline_compare()
{
	u16 disp_stat = mem->read_u16_fast(DISPSTAT);
	u8 lyc = disp_stat >> 8;
	
	//Raise VCOUNT interrupt
	if(current_scanline == lyc)
	{
		if(mem->memory_map[DISPSTAT] & 0x20) 
		{ 
			mem->memory_map[REG_IF] |= 0x4; 
		}

		//Toggle VCOUNT flag ON
		disp_stat |= 0x4;
		mem->write_u16_fast(DISPSTAT, disp_stat);
	}

	else
	{
		//Toggle VCOUNT flag OFF
		disp_stat &= ~0x4;
		mem->write_u16_fast(DISPSTAT, disp_stat);
	}
}
