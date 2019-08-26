#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "mxml.h"

#define ATB_TEXTURE_FORMAT_RGBA8 0
#define ATB_TEXTURE_FORMAT_RGB5A3 1
#define ATB_TEXTURE_FORMAT_RGB5A3_DUPE 2
#define ATB_TEXTURE_FORMAT_CI8 3
#define ATB_TEXTURE_FORMAT_CI4 4
#define ATB_TEXTURE_FORMAT_IA8 5
#define ATB_TEXTURE_FORMAT_IA4 6
#define ATB_TEXTURE_FORMAT_I8 7
#define ATB_TEXTURE_FORMAT_I4 8
#define ATB_TEXTURE_FORMAT_A8 9
#define ATB_TEXTURE_FORMAT_CMPR 10
#define GX_TEXTURE_FORMAT_I4 0
#define GX_TEXTURE_FORMAT_I8 1
#define GX_TEXTURE_FORMAT_IA4 2
#define GX_TEXTURE_FORMAT_IA8 3
#define GX_TEXTURE_FORMAT_RGB5A3 5
#define GX_TEXTURE_FORMAT_RGBA8 6
#define GX_TEXTURE_FORMAT_CI4 8
#define GX_TEXTURE_FORMAT_CI8 9
#define GX_TEXTURE_FORMAT_CMPR 14
#define GX_TEXTURE_FORMAT_A8 39
#define GX_TLUT_FORMAT_RGB5A3 2

#define GX_CLAMP 0
#define GX_LINEAR 1
#define TEXTURE_ALIGNMENT 32

#define FLIP_NONE 0
#define FLIP_X (1 << 0)
#define FLIP_Y (1 << 1)

typedef struct atb_file
{
	short num_banks;
	short num_patterns;
	short num_textures;
	short num_references;
	int bank_data_offset;
	int pattern_data_offset;
	int texture_data_offset;
} AtbFile;

typedef struct atb_bank_data
{
	short frame_count;
	short pad;
	int first_anim_frame_offset;
} AtbBankData;

typedef struct anim_frame
{
	short pattern_index;
	short frame_length; //Equal to -1 equals Last Frame of Animation
	short shift_x;
	short shift_y;
	short flip;
	short unk;
} AnimFrame;

typedef struct atb_pattern_data
{
	short num_layers;
	short center_x;
	short center_y;
	short width;
	short height;
	short pad;
	int first_layer_offset;
} AtbPatternData;

typedef struct layer_data
{
	unsigned char alpha;
	char flip;
	short texture_index;
	short texcoord_upper_left_x;
	short texcoord_upper_left_y;
	short texcoord_width;
	short texcoord_height;
	short shift_x;
	short shift_y;
	short upper_left_vertex_x;
	short upper_left_vertex_y;
	short upper_right_vertex_x;
	short upper_right_vertex_y;
	short lower_right_vertex_x;
	short lower_right_vertex_y;
	short lower_left_vertex_x;
	short lower_left_vertex_y;
} LayerData;

typedef struct atb_texture
{
	char bpp;
	char format;
	short palette_len;
	short width;
	short height;
	int bitmap_len;
	int palette_offset;
	int bitmap_offset;
} AtbTexture;

typedef struct tpl_header
{
	int magic;
	int num_textures;
	int offset_table_offset;
} TplHeader;

typedef struct tpl_image_description
{
	int image_offset;
	int palette_offset;
} TplImageDescription;

typedef struct tpl_image_header
{
	unsigned short height;
	unsigned short width;
	unsigned int format;
	int data_offset;
	unsigned int wraps;
	unsigned int wrapt;
	unsigned int minfilter;
	unsigned int magfilter;
	float lodbias;
	unsigned char edgelod;
	unsigned char minlod;
	unsigned char maxlod;
	unsigned char unpacked;
} TplImageHeader;

typedef struct tpl_palette_header {
	unsigned short num_entries;
	unsigned char unpacked;
	unsigned char pad;
	unsigned int format;
	int data_offset;
} TplPaletteHeader;

int main(int argc, char** argv);
int AtbtoGxTextureFormat(int atb_format);
unsigned char ReadFileU8(FILE *fp, int offset);
void ReadFileArray(FILE *fp, void *array, int offset, int len); //Assumes U8 Array
unsigned short ReadFileU16BigEndian(FILE *fp, int offset);
unsigned int ReadFileU32BigEndian(FILE *fp, int offset);
void WriteFileU8(FILE *fp, int offset, unsigned char num);
void WriteFileArray(FILE *fp, void *array, int offset, int len); //Assumes U8 Array
void WriteFileU16BigEndian(FILE *fp, int offset, unsigned short num);
void WriteFileU32BigEndian(FILE *fp, int offset, unsigned int num);
void WriteFileFloatBigEndian(FILE *fp, int offset, float num);
const char *WhitespaceCallback(mxml_node_t *node, int where);

int main(int argc, char** argv)
{
	FILE *atb_fptr;
	FILE *tpl_fptr;
	FILE *xml_fptr;
	AtbBankData *parsed_atb_banks = NULL;
	AtbPatternData *parsed_atb_patterns = NULL;
	AtbTexture *parsed_atb_textures = NULL;
	AnimFrame *parsed_frames = NULL;
	LayerData *parsed_layers = NULL;
	TplImageHeader *tpl_images = NULL;
	TplPaletteHeader *tpl_palettes = NULL;
	char tpl_name[256];
	char xml_name[256];
	mxml_node_t *xml;    /* <?xml ... ?> */
	mxml_node_t *data;   /* <data> */
	mxml_node_t *group = NULL;   /* <data> */
	mxml_node_t *node;   /* <node> */
	int tpl_path_len;
	int xml_path_len;
	int tpl_magic = 0x20AF30;
	int i;
	int j;
	int num_palettes;
	int palette_idx;
	int texture_idx;
	unsigned short *palette_associations = NULL;
	unsigned short *frame_associations = NULL;
	unsigned short *layer_associations = NULL;
	void *bitmap = malloc(4194304); //Size of RGBA8 image of 1024x1024 size
	void *palette = malloc(8192); //Allocate Room for 16 256-Color Palettes
	
	if (argc >= 2)
	{
		atb_fptr = fopen(argv[1], "rb");
		if (atb_fptr == NULL)
		{
			printf("Couldn't Open File %s", argv[1]);
			return 0;
		}
		if (bitmap == NULL)
		{
			printf("Failed to Allocate Bitmap.");
			return 0;
		}
		if (palette == NULL)
		{
			printf("Failed to Allocate Palette.");
			return 0;
		}
		strncpy(xml_name, argv[1], sizeof(xml_name));
		xml_path_len = strlen(xml_name);
		xml_name[xml_path_len - 3] = 'x';
		xml_name[xml_path_len - 2] = 'm';
		xml_name[xml_path_len - 1] = 'l';
		xml_fptr = fopen(xml_name, "wb");
		if (xml_fptr == NULL)
		{
			printf("Failed to Create XML File %s", xml_name);
			return 0;
		}
		strncpy(tpl_name, argv[1], sizeof(tpl_name));
		tpl_path_len = strlen(tpl_name);
		tpl_name[tpl_path_len - 3] = 't';
		tpl_name[tpl_path_len - 2] = 'p';
		tpl_name[tpl_path_len - 1] = 'l';
		tpl_fptr = fopen(tpl_name, "wb");
		if (tpl_fptr == NULL)
		{
			printf("Failed to Create TPL %s", tpl_name);
			return 0;
		}
		int num_banks = ReadFileU16BigEndian(atb_fptr, offsetof(AtbFile, num_banks));
		int num_patterns = ReadFileU16BigEndian(atb_fptr, offsetof(AtbFile, num_patterns));
		int num_textures = ReadFileU16BigEndian(atb_fptr, offsetof(AtbFile, num_textures));
		int atb_bank_base = ReadFileU32BigEndian(atb_fptr, offsetof(AtbFile, bank_data_offset));
		int atb_pattern_base = ReadFileU32BigEndian(atb_fptr, offsetof(AtbFile, pattern_data_offset));
		int atb_texture_base = ReadFileU32BigEndian(atb_fptr, offsetof(AtbFile, texture_data_offset));
		WriteFileU32BigEndian(tpl_fptr, offsetof(TplHeader, magic), tpl_magic);
		WriteFileU32BigEndian(tpl_fptr, offsetof(TplHeader, num_textures), num_textures);
		WriteFileU32BigEndian(tpl_fptr, offsetof(TplHeader, offset_table_offset), sizeof(TplHeader));
		int tpl_desription_start = sizeof(TplHeader);
		int tpl_texture_header_start = sizeof(TplHeader) + (num_textures * sizeof(TplImageDescription));
		int tpl_palette_header_start = tpl_texture_header_start + (num_textures * sizeof(TplImageHeader));
		int curr_atb_texture = atb_texture_base;
		parsed_atb_banks = malloc(sizeof(AtbBankData)*num_banks);
		parsed_atb_patterns = malloc(sizeof(AtbPatternData)*num_patterns);
		parsed_atb_textures = malloc(sizeof(AtbTexture)*num_textures);
		tpl_images = malloc(sizeof(TplImageHeader)*num_textures);

		num_palettes = 0;
		for (i = 0; i < num_textures; i++)
		{
			parsed_atb_textures[i].bpp = ReadFileU8(atb_fptr, curr_atb_texture);
			parsed_atb_textures[i].format = ReadFileU8(atb_fptr, curr_atb_texture + offsetof(AtbTexture, format));
			parsed_atb_textures[i].palette_len = ReadFileU16BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, palette_len));
			parsed_atb_textures[i].width = ReadFileU16BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, width));
			parsed_atb_textures[i].height = ReadFileU16BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, height));
			parsed_atb_textures[i].bitmap_len = ReadFileU32BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, bitmap_len));
			parsed_atb_textures[i].palette_offset = ReadFileU32BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, palette_offset));
			parsed_atb_textures[i].bitmap_offset = ReadFileU32BigEndian(atb_fptr, curr_atb_texture + offsetof(AtbTexture, bitmap_offset));
			if (parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI8 || parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI4)
			{
				num_palettes++;
			}
			curr_atb_texture += sizeof(AtbTexture);
		}
		int image_data = tpl_palette_header_start + (num_palettes * sizeof(TplPaletteHeader));
		image_data = ((image_data - 1)|(TEXTURE_ALIGNMENT-1))+1; //Align Image Data Start to 32 Bytes
		if (num_palettes != 0)
		{
			tpl_palettes = malloc(sizeof(TplPaletteHeader)*num_palettes);
			palette_associations = malloc(sizeof(unsigned short)*num_palettes);
			palette_idx = 0;
		}
		for (i = 0; i < num_textures; i++)
		{
			tpl_images[i].height = parsed_atb_textures[i].height;
			tpl_images[i].width = parsed_atb_textures[i].width;
			tpl_images[i].format = AtbtoGxTextureFormat(parsed_atb_textures[i].format);
			tpl_images[i].data_offset = image_data;
			tpl_images[i].wraps = GX_CLAMP;
			tpl_images[i].wrapt = GX_CLAMP;
			tpl_images[i].minfilter = GX_LINEAR;
			tpl_images[i].magfilter = GX_LINEAR;
			tpl_images[i].lodbias = 0.0f;
			tpl_images[i].edgelod = 0;
			tpl_images[i].minlod = 0;
			tpl_images[i].maxlod = 0;
			if (parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI8 || parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI4)
			{
				palette_associations[palette_idx] = i;
				palette_idx++;
			}
			image_data += parsed_atb_textures[i].bitmap_len;
			image_data = ((image_data - 1) | (TEXTURE_ALIGNMENT - 1)) + 1; //Align Next Image Data Start to 32 Bytes
		}
		int palette_data = image_data;
		palette_data = ((palette_data - 1) | (TEXTURE_ALIGNMENT - 1)) + 1; //Align Palette Data Start to 32 Bytes
		palette_idx = 0;
		for (i = 0; i < num_palettes; i++)
		{
			texture_idx = palette_associations[palette_idx];
			tpl_palettes[i].num_entries = parsed_atb_textures[texture_idx].palette_len;
			tpl_palettes[i].format = GX_TLUT_FORMAT_RGB5A3;
			tpl_palettes[i].data_offset = palette_data;
			palette_data += (parsed_atb_textures[texture_idx].palette_len*2);
			palette_data = ((palette_data - 1) | (TEXTURE_ALIGNMENT - 1)) + 1; //Align Next Palette Data to 32 Bytes
			palette_idx++;
		}
		int tpl_desription = tpl_desription_start;
		int tpl_texture_header = tpl_texture_header_start;
		int tpl_palette_header = tpl_palette_header_start;
		palette_idx = 0;
		for (i = 0; i < num_textures; i++)
		{
			WriteFileU32BigEndian(tpl_fptr, tpl_desription, tpl_texture_header);
			WriteFileU16BigEndian(tpl_fptr, tpl_texture_header, tpl_images[i].height);
			WriteFileU16BigEndian(tpl_fptr, tpl_texture_header+offsetof(TplImageHeader, width), tpl_images[i].width);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, format), tpl_images[i].format);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, data_offset), tpl_images[i].data_offset);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, wraps), tpl_images[i].wraps);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, wrapt), tpl_images[i].wrapt);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, minfilter), tpl_images[i].minfilter);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, magfilter), tpl_images[i].magfilter);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, minfilter), tpl_images[i].minfilter);
			WriteFileU32BigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, magfilter), tpl_images[i].magfilter);
			WriteFileFloatBigEndian(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, lodbias), tpl_images[i].lodbias);
			WriteFileU8(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, edgelod), tpl_images[i].edgelod);
			WriteFileU8(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, minlod), tpl_images[i].minlod);
			WriteFileU8(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, maxlod), tpl_images[i].maxlod);
			WriteFileU8(tpl_fptr, tpl_texture_header + offsetof(TplImageHeader, unpacked), 0);
			ReadFileArray(atb_fptr, bitmap, parsed_atb_textures[i].bitmap_offset, parsed_atb_textures[i].bitmap_len);
			WriteFileArray(tpl_fptr, bitmap, tpl_images[i].data_offset, parsed_atb_textures[i].bitmap_len);
			if (parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI8 || parsed_atb_textures[i].format == ATB_TEXTURE_FORMAT_CI4)
			{
				WriteFileU32BigEndian(tpl_fptr, tpl_desription+offsetof(TplImageDescription, palette_offset), tpl_palette_header);
				WriteFileU16BigEndian(tpl_fptr, tpl_palette_header, tpl_palettes[palette_idx].num_entries);
				WriteFileU8(tpl_fptr, tpl_palette_header + offsetof(TplPaletteHeader, unpacked), 0);
				WriteFileU8(tpl_fptr, tpl_palette_header + offsetof(TplPaletteHeader, pad), 0);
				WriteFileU32BigEndian(tpl_fptr, tpl_palette_header + offsetof(TplPaletteHeader, format), tpl_palettes[palette_idx].format);
				WriteFileU32BigEndian(tpl_fptr, tpl_palette_header + offsetof(TplPaletteHeader, data_offset), tpl_palettes[palette_idx].data_offset);
				ReadFileArray(atb_fptr, bitmap, parsed_atb_textures[i].palette_offset, parsed_atb_textures[i].palette_len*2);
				WriteFileArray(tpl_fptr, bitmap, tpl_palettes[palette_idx].data_offset, parsed_atb_textures[i].palette_len * 2);
				tpl_palette_header += sizeof(TplPaletteHeader);
				palette_idx++;
			}
			else
			{
				WriteFileU32BigEndian(tpl_fptr, tpl_desription + offsetof(TplImageDescription, palette_offset), 0);
			}
			tpl_desription += sizeof(TplImageDescription);
			tpl_texture_header += sizeof(TplImageHeader);
		}
		int curr_atb_bank = atb_bank_base;
		int num_frames = 0;
		for (i = 0; i < num_banks; i++)
		{
			parsed_atb_banks[i].frame_count = ReadFileU16BigEndian(atb_fptr, curr_atb_bank);
			parsed_atb_banks[i].first_anim_frame_offset = ReadFileU32BigEndian(atb_fptr, curr_atb_bank + offsetof(AtbBankData, first_anim_frame_offset));
			num_frames += parsed_atb_banks[i].frame_count;
			curr_atb_bank += sizeof(AtbBankData);
		}
		parsed_frames = malloc(num_frames * sizeof(AnimFrame));
		frame_associations = malloc(num_frames * sizeof(unsigned short));
		int curr_frame_idx = 0;
		int curr_frame_offset = 0;
		for (i = 0; i < num_banks; i++)
		{
			curr_frame_offset = parsed_atb_banks[i].first_anim_frame_offset;
			frame_associations[i] = curr_frame_idx;
			for (j = 0; j < parsed_atb_banks[i].frame_count; j++)
			{
				parsed_frames[curr_frame_idx].pattern_index = ReadFileU16BigEndian(atb_fptr, curr_frame_offset);
				parsed_frames[curr_frame_idx].frame_length = ReadFileU16BigEndian(atb_fptr, curr_frame_offset +offsetof(AnimFrame, frame_length));
				parsed_frames[curr_frame_idx].shift_x = ReadFileU16BigEndian(atb_fptr, curr_frame_offset + offsetof(AnimFrame, shift_x));
				parsed_frames[curr_frame_idx].shift_y = ReadFileU16BigEndian(atb_fptr, curr_frame_offset + offsetof(AnimFrame, shift_y));
				parsed_frames[curr_frame_idx].flip = ReadFileU16BigEndian(atb_fptr, curr_frame_offset + offsetof(AnimFrame, flip));
				curr_frame_offset += sizeof(AnimFrame);
				curr_frame_idx++;
			}
		}
		int curr_atb_pattern = atb_pattern_base;
		int num_layers = 0;
		for (i = 0; i < num_patterns; i++)
		{
			parsed_atb_patterns[i].num_layers = ReadFileU16BigEndian(atb_fptr, curr_atb_pattern);
			parsed_atb_patterns[i].center_x = ReadFileU16BigEndian(atb_fptr, curr_atb_pattern + offsetof(AtbPatternData, center_x));
			parsed_atb_patterns[i].center_y = ReadFileU16BigEndian(atb_fptr, curr_atb_pattern + offsetof(AtbPatternData, center_y));
			parsed_atb_patterns[i].width = ReadFileU16BigEndian(atb_fptr, curr_atb_pattern + offsetof(AtbPatternData, width));
			parsed_atb_patterns[i].height = ReadFileU16BigEndian(atb_fptr, curr_atb_pattern + offsetof(AtbPatternData, height));
			parsed_atb_patterns[i].first_layer_offset = ReadFileU32BigEndian(atb_fptr, curr_atb_pattern + offsetof(AtbPatternData, first_layer_offset));
			num_layers += parsed_atb_patterns[i].num_layers;
			curr_atb_pattern += sizeof(AtbPatternData);
		}
		parsed_layers = malloc(num_layers * sizeof(LayerData));
		layer_associations = malloc(num_layers * sizeof(unsigned short));
		int curr_layer_idx = 0;
		int curr_layer_offset = 0;
		for (i = 0; i < num_patterns; i++)
		{
			curr_layer_offset = parsed_atb_patterns[i].first_layer_offset;
			layer_associations[i] = curr_layer_idx;
			for (j = 0; j < parsed_atb_patterns[i].num_layers; j++)
			{
				parsed_layers[curr_layer_idx].alpha = ReadFileU8(atb_fptr, curr_layer_offset);
				parsed_layers[curr_layer_idx].flip = ReadFileU8(atb_fptr, curr_layer_offset + offsetof(LayerData, flip));
				parsed_layers[curr_layer_idx].texture_index = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, texture_index));
				parsed_layers[curr_layer_idx].texcoord_upper_left_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, texcoord_upper_left_x));
				parsed_layers[curr_layer_idx].texcoord_upper_left_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, texcoord_upper_left_y));
				parsed_layers[curr_layer_idx].texcoord_width = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, texcoord_width));
				parsed_layers[curr_layer_idx].texcoord_height = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, texcoord_height));
				parsed_layers[curr_layer_idx].shift_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, shift_x));
				parsed_layers[curr_layer_idx].shift_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, shift_y));
				parsed_layers[curr_layer_idx].upper_left_vertex_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, upper_left_vertex_x));
				parsed_layers[curr_layer_idx].upper_left_vertex_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, upper_left_vertex_y));
				parsed_layers[curr_layer_idx].upper_right_vertex_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, upper_right_vertex_x));
				parsed_layers[curr_layer_idx].upper_right_vertex_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, upper_right_vertex_y));
				parsed_layers[curr_layer_idx].lower_right_vertex_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, lower_right_vertex_x));
				parsed_layers[curr_layer_idx].lower_right_vertex_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, lower_right_vertex_y));
				parsed_layers[curr_layer_idx].lower_left_vertex_x = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, lower_left_vertex_x));
				parsed_layers[curr_layer_idx].lower_left_vertex_y = ReadFileU16BigEndian(atb_fptr, curr_layer_offset + offsetof(LayerData, lower_left_vertex_y));
				curr_layer_offset += sizeof(LayerData);
				curr_layer_idx++;
			}
		}
		xml = mxmlNewXML("1.0");
		data = mxmlNewElement(xml, "banks");
		for (i = 0; i < num_banks; i++)
		{
			char name[256];
			sprintf(name, "BANK%d", i);
			group = mxmlNewElement(data, "bank");
			mxmlElementSetAttr(group, "name", name);
			node = mxmlNewElement(group, "frame_count");
			mxmlNewInteger(node, parsed_atb_banks[i].frame_count);
			sprintf(name, "FRAME%d", frame_associations[i]);
			node = mxmlNewElement(group, "first_frame");
			mxmlNewText(node, 0, name);
		}
		data = mxmlNewElement(xml, "frames");
		curr_frame_idx = 0;
		for (i = 0; i < num_banks; i++)
		{
			char name[256];
			sprintf(name, "FRAME%d", frame_associations[i]);
			group = mxmlNewElement(data, "frame");
			mxmlElementSetAttr(group, "name", name);
			curr_frame_idx = frame_associations[i];
			for (j = 0; j < parsed_atb_banks[i].frame_count; j++)
			{
				if (parsed_frames[curr_frame_idx + j].frame_length == -1)
				{
					node = mxmlNewElement(group, "pattern");
					mxmlNewText(node, 0, "NULL");
					node = mxmlNewElement(group, "frame_length");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].frame_length);
					node = mxmlNewElement(group, "shift_x");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].shift_x);
					node = mxmlNewElement(group, "shift_y");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].shift_y);
					node = mxmlNewElement(group, "flip");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].flip);
				}
				else
				{
					sprintf(name, "PATTERN%d", parsed_frames[curr_frame_idx + j].pattern_index);
					node = mxmlNewElement(group, "pattern");
					mxmlNewText(node, 0, name);
					node = mxmlNewElement(group, "frame_length");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].frame_length);
					node = mxmlNewElement(group, "shift_x");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].shift_x);
					node = mxmlNewElement(group, "shift_y");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].shift_y);
					node = mxmlNewElement(group, "flip");
					mxmlNewInteger(node, parsed_frames[curr_frame_idx + j].flip);
				}
			}
		}
		data = mxmlNewElement(xml, "patterns");
		for (i = 0; i < num_patterns; i++)
		{
			char name[256];
			sprintf(name, "PATTERN%d", i);
			group = mxmlNewElement(data, "pattern");
			mxmlElementSetAttr(group, "name", name);
			node = mxmlNewElement(group, "layer_count");
			mxmlNewInteger(node, parsed_atb_patterns[i].num_layers);
			node = mxmlNewElement(group, "center_x");
			mxmlNewInteger(node, parsed_atb_patterns[i].center_x);
			node = mxmlNewElement(group, "center_y");
			mxmlNewInteger(node, parsed_atb_patterns[i].center_y);
			node = mxmlNewElement(group, "width");
			mxmlNewInteger(node, parsed_atb_patterns[i].width);
			node = mxmlNewElement(group, "height");
			mxmlNewInteger(node, parsed_atb_patterns[i].height);
			sprintf(name, "LAYER%d", layer_associations[i]);
			node = mxmlNewElement(group, "layer");
			mxmlNewText(node, 0, name);
		}
		curr_layer_idx = 0;
		data = mxmlNewElement(xml, "layers");
		for (i = 0; i < num_patterns; i++)
		{
			char name[256];
			sprintf(name, "LAYER%d", layer_associations[i]);
			group = mxmlNewElement(data, "layer");
			mxmlElementSetAttr(group, "name", name);
			curr_layer_idx = layer_associations[i];
			for (j = 0; j < parsed_atb_patterns[i].num_layers; j++)
			{
				node = mxmlNewElement(group, "alpha");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].alpha);
				node = mxmlNewElement(group, "flip");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].flip);
				node = mxmlNewElement(group, "texture_index");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].texture_index);
				node = mxmlNewElement(group, "texcoord_upper_left_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].texcoord_upper_left_x);
				node = mxmlNewElement(group, "texcoord_upper_left_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].texcoord_upper_left_y);
				node = mxmlNewElement(group, "texcoord_width");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].texcoord_width);
				node = mxmlNewElement(group, "texcoord_height");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].texcoord_height);
				node = mxmlNewElement(group, "shift_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].shift_x);
				node = mxmlNewElement(group, "shift_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].shift_y);
				node = mxmlNewElement(group, "upper_left_vertex_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].upper_left_vertex_x);
				node = mxmlNewElement(group, "upper_left_vertex_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].upper_left_vertex_y);
				node = mxmlNewElement(group, "upper_right_vertex_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].upper_right_vertex_x);
				node = mxmlNewElement(group, "upper_right_vertex_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].upper_right_vertex_y);
				node = mxmlNewElement(group, "lower_right_vertex_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].lower_right_vertex_x);
				node = mxmlNewElement(group, "lower_right_vertex_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].lower_right_vertex_y);
				node = mxmlNewElement(group, "lower_left_vertex_x");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].lower_left_vertex_x);
				node = mxmlNewElement(group, "lower_left_vertex_y");
				mxmlNewInteger(node, parsed_layers[curr_layer_idx + j].lower_left_vertex_y);
			}
		}
		data = mxmlNewElement(xml, "texture");
		mxmlNewText(data, NULL, tpl_name);
		mxmlSaveFile(xml, xml_fptr, WhitespaceCallback);
		fclose(tpl_fptr);
		fclose(xml_fptr);
		fclose(atb_fptr);
		return 1;
	}
	else
	{
		printf("Usage is atbdump atb_file.");
		return 0;
	}
}

const char *WhitespaceCallback(mxml_node_t *node, int where)
{
	const char *element;
	element = mxmlGetElement(node);

	if (strcmp(element, "banks") == 0 || strcmp(element, "frames") == 0 || strcmp(element, "patterns") == 0
		|| strcmp(element, "layers") == 0)
	{
		if (where == MXML_WS_BEFORE_OPEN || where == MXML_WS_BEFORE_CLOSE || where == MXML_WS_AFTER_CLOSE)
		{
			return("\n");
		}
	}
	else
	{
		if (strcmp(element, "bank") == 0 && mxmlElementGetAttr(node, "name") != NULL ||
			strcmp(element, "frame") == 0 && mxmlElementGetAttr(node, "name") != NULL ||
			strcmp(element, "pattern") == 0 && mxmlElementGetAttr(node, "name") != NULL ||
			strcmp(element, "layer") == 0 && mxmlElementGetAttr(node, "name") != NULL)
		{
			if (where == MXML_WS_BEFORE_OPEN || where == MXML_WS_AFTER_OPEN)
			{
				return("\n");
			}
			else
			{
				return NULL;
			}
		}
	}

	if (strcmp(element, "frame_count") == 0 || strcmp(element, "first_frame") == 0)
	{
		if (where == MXML_WS_AFTER_CLOSE)
		{
			return("\n");
		}
		if (where == MXML_WS_BEFORE_OPEN)
		{
			return("\t");
		}
	}
	if (strcmp(element, "pattern") == 0 || strcmp(element, "frame_length") == 0 || strcmp(element, "shift_x") == 0
		|| strcmp(element, "shift_y") == 0 || strcmp(element, "flip") == 0 || strcmp(element, "") == 0)
	{
		if (where == MXML_WS_AFTER_CLOSE)
		{
			return("\n");
		}
		if (where == MXML_WS_BEFORE_OPEN)
		{
			return("\t");
		}
	}
	if (strcmp(element, "layer_count") == 0 || strcmp(element, "center_x") == 0 || strcmp(element, "center_y") == 0
		|| strcmp(element, "width") == 0 || strcmp(element, "height") == 0 || strcmp(element, "layer") == 0)
	{
		if (where == MXML_WS_AFTER_CLOSE)
		{
			return("\n");
		}
		if (where == MXML_WS_BEFORE_OPEN)
		{
			return("\t");
		}
	}
	if (strcmp(element, "alpha") == 0 || strcmp(element, "texture_index") == 0 || strcmp(element, "texcoord_upper_left_x") == 0 || strcmp(element, "texcoord_upper_left_y") == 0
		|| strcmp(element, "texcoord_width") == 0 || strcmp(element, "texcoord_height") == 0 || strcmp(element, "upper_left_vertex_x") == 0
		|| strcmp(element, "upper_left_vertex_y") == 0 || strcmp(element, "upper_right_vertex_x") == 0 || strcmp(element, "upper_right_vertex_y") == 0
		|| strcmp(element, "lower_right_vertex_x") == 0 || strcmp(element, "lower_right_vertex_y") == 0 || strcmp(element, "lower_left_vertex_x") == 0
		|| strcmp(element, "lower_left_vertex_y") == 0)
	{
		if (where == MXML_WS_AFTER_CLOSE)
		{
			return("\n");
		}
		if (where == MXML_WS_BEFORE_OPEN)
		{
			return("\t");
		}
	}
	if (strncmp(element, "?xml", 4) == 0)
	{
		if (where == MXML_WS_AFTER_OPEN)
		{
			return("\n");
		}
	}
	return (NULL);
}

int AtbtoGxTextureFormat(int atb_format)
{
	switch (atb_format)
	{
		case ATB_TEXTURE_FORMAT_RGBA8:
			return GX_TEXTURE_FORMAT_RGBA8;

		case ATB_TEXTURE_FORMAT_RGB5A3:
		case ATB_TEXTURE_FORMAT_RGB5A3_DUPE:
			return GX_TEXTURE_FORMAT_RGB5A3;

		case ATB_TEXTURE_FORMAT_CI8:
			return GX_TEXTURE_FORMAT_CI8;

		case ATB_TEXTURE_FORMAT_CI4:
			return GX_TEXTURE_FORMAT_CI4;

		case ATB_TEXTURE_FORMAT_IA8:
			return GX_TEXTURE_FORMAT_IA8;

		case ATB_TEXTURE_FORMAT_IA4:
			return GX_TEXTURE_FORMAT_IA4;

		case ATB_TEXTURE_FORMAT_I8:
			return GX_TEXTURE_FORMAT_I8;

		case ATB_TEXTURE_FORMAT_I4:
			return GX_TEXTURE_FORMAT_I4;

		case ATB_TEXTURE_FORMAT_A8:
			return GX_TEXTURE_FORMAT_A8;

		case ATB_TEXTURE_FORMAT_CMPR:
			return GX_TEXTURE_FORMAT_CMPR;

		default:
			printf("Invalid Texture Format.");
			return 0;
	}

}

unsigned char ReadFileU8(FILE *fp, int offset)
{
	unsigned char temp;
	fseek(fp, offset, SEEK_SET);
	fread(&temp, sizeof(unsigned char), 1, fp);
	return temp;
}

void ReadFileArray(FILE *fp, void *array, int offset, int len)
{
	fseek(fp, offset, SEEK_SET);
	fread(array, sizeof(unsigned char), len, fp);
}

unsigned short ReadFileU16BigEndian(FILE *fp, int offset)
{
	unsigned short temp;
	unsigned char temp_buf[2];
	fseek(fp, offset, SEEK_SET);
	fread(&temp_buf, sizeof(unsigned char), 2, fp);
	temp = temp_buf[0] << 8 | temp_buf[1];
	return temp;
}

unsigned int ReadFileU32BigEndian(FILE *fp, int offset)
{
	unsigned int temp;
	unsigned char temp_buf[4];
	fseek(fp, offset, SEEK_SET);
	fread(&temp_buf, sizeof(unsigned char), 4, fp);
	temp = temp_buf[0] << 24 | temp_buf[1] << 16 | temp_buf[2] << 8 | temp_buf[3];
	return temp;
}

float ReadFileFloatBigEndian(FILE *fp, int offset)
{
	unsigned int temp;
	unsigned char temp_buf[4];
	fseek(fp, offset, SEEK_SET);
	fread(&temp_buf, sizeof(unsigned char), 4, fp);
	temp = temp_buf[0] << 24 | temp_buf[1] << 16 | temp_buf[2] << 8 | temp_buf[3];
	return *(float*)&temp;
}

void WriteFileU8(FILE *fp, int offset, unsigned char num)
{
	unsigned char temp = num;
	fseek(fp, offset, SEEK_SET);
	fwrite(&temp, sizeof(unsigned char), 1, fp);
}

void WriteFileArray(FILE *fp, void *array, int offset, int len)
{
	fseek(fp, offset, SEEK_SET);
	fwrite(array, sizeof(unsigned char), len, fp);
}

void WriteFileU16BigEndian(FILE *fp, int offset, unsigned short num)
{
	unsigned short temp = 0;
	temp |= (num & 0xff) << 8;
	temp |= (num & 0xff00) >> 8;
	fseek(fp, offset, SEEK_SET);
	fwrite(&temp, sizeof(unsigned char), 2, fp);
}

void WriteFileU32BigEndian(FILE *fp, int offset, unsigned int num)
{
	unsigned int temp = 0;
	temp |= (num & 0x000000FF) << 24;
	temp |= (num & 0x0000FF00) << 8;
	temp |= (num & 0x00FF0000) >> 8;
	temp |= (num & 0xFF000000) >> 24;
	fseek(fp, offset, SEEK_SET);
	fwrite(&temp, sizeof(unsigned int), 1, fp);
}

void WriteFileFloatBigEndian(FILE *fp, int offset, float num)
{
	WriteFileU32BigEndian(fp, offset, *(unsigned int*)&num);
}