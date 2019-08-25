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

int main(int argc, char **argv);
int GetTextureFormatBpp(int gx_format);
int GxtoAtbTextureFormat(int gx_format);
int FindStringinArray(char **str_array, char *str, int array_entries);
unsigned char ReadFileU8(FILE *fp, int offset);
void ReadFileArray(FILE *fp, void *array, int offset, int len); //Assumes U8 Array
unsigned short ReadFileU16BigEndian(FILE *fp, int offset);
unsigned int ReadFileU32BigEndian(FILE *fp, int offset);
void WriteFileU8(FILE *fp, int offset, unsigned char num);
void WriteFileArray(FILE *fp, void *array, int offset, int len); //Assumes U8 Array
void WriteFileU16BigEndian(FILE *fp, int offset, unsigned short num);
void WriteFileU32BigEndian(FILE *fp, int offset, unsigned int num);
void WriteFileFloatBigEndian(FILE *fp, int offset, float num);

int main(int argc, char **argv)
{
	FILE *out_fptr;
	FILE *tpl_fptr;
	FILE *xml_fptr;
	mxml_node_t *tree;
	mxml_node_t *bank_node;
	mxml_node_t *frame_node;
	mxml_node_t *pattern_node;
	mxml_node_t *layer_node;
	mxml_node_t *temp_node;
	int num_banks = 0;
	int num_frames = 0;
	int num_patterns = 0;
	int num_layers = 0;
	int num_textures = 0;
	int pattern_offset = sizeof(AtbFile);
	int layer_data_offset = 0;
	int bank_data_offset = 0;
	int frame_offset = 0;
	int texture_offset = 0;
	int texture_data_offset = 0;
	int tpl_texture_offset = 0;
	int i;
	char **frame_names;
	char **pattern_names;
	char **layer_names;
	AtbBankData *banks;
	AnimFrame *frames;
	AtbPatternData *patterns;
	LayerData *layers;
	AtbTexture *textures;
	void *bitmap = malloc(4194304); //Size of RGBA8 image of 1024x1024 size
	void *palette = malloc(8192); //Allocate Room for 16 256-Color Palettes

	if (argc >= 4)
	{
		out_fptr = fopen(argv[1], "wb");
		tpl_fptr = fopen(argv[2], "rb");
		xml_fptr = fopen(argv[3], "r");
		if (out_fptr == NULL)
		{
			printf("Failed to Open File %s for writing.", argv[1]);
			getchar();
			return 0;
		}
		if (tpl_fptr == NULL)
		{
			printf("Failed to Open File %s for reading.", argv[2]);
			getchar();
			return 0;
		}
		if (xml_fptr == NULL)
		{
			printf("Failed to Open File %s for reading.", argv[3]);
			getchar();
			return 0;
		}
		tree = mxmlLoadFile(NULL, xml_fptr, MXML_TEXT_CALLBACK);
		fclose(xml_fptr);
		bank_node = mxmlFindElement(tree, tree, "banks", NULL, NULL, MXML_DESCEND);
		temp_node = mxmlGetFirstChild(bank_node);
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("bank", name) == 0)
			{
				num_banks++;
			}
		}
		banks = malloc(num_banks * sizeof(AtbBankData));
		frame_node = mxmlFindElement(tree, tree, "frames", NULL, NULL, MXML_DESCEND);
		temp_node = mxmlGetFirstChild(frame_node);
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("frame", name) == 0)
			{
				i = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						i++;
					}
				}
				num_frames += (i / 5);
			}
		}
		frames = malloc(num_frames * sizeof(AnimFrame));
		frame_names = malloc(num_frames * sizeof(char *));
		for (i = 0; i < num_frames; i++)
		{
			frame_names[i] = NULL;
		}
		pattern_node = mxmlFindElement(tree, tree, "patterns", NULL, NULL, MXML_DESCEND);
		temp_node = mxmlGetFirstChild(pattern_node);
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("pattern", name) == 0)
			{
				num_patterns++;
			}
		}
		patterns = malloc(num_patterns * sizeof(AtbPatternData));
		pattern_names = malloc(num_patterns * sizeof(char *));
		for (i = 0; i < num_patterns; i++)
		{
			pattern_names[i] = NULL;
		}
		layer_node = mxmlFindElement(tree, tree, "layers", NULL, NULL, MXML_DESCEND);
		temp_node = mxmlGetFirstChild(layer_node);
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("layer", name) == 0)
			{
				i = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						i++;
					}
				}
				num_layers += (i / 17);
			}
		}
		layers = malloc(num_layers * sizeof(LayerData));
		layer_names = malloc(num_layers * sizeof(char *));
		for (i = 0; i < num_layers; i++)
		{
			layer_names[i] = NULL;
		}
		temp_node = mxmlGetFirstChild(frame_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("frame", name) == 0)
			{
				frame_names[i] = mxmlElementGetAttr(temp_node, "name");
				int j = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						j++;
					}
				}
				i += (j / 5);
			}
		}
		temp_node = mxmlGetFirstChild(pattern_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("pattern", name) == 0)
			{
				pattern_names[i] = mxmlElementGetAttr(temp_node, "name");
				i++;
			}
		}
		temp_node = mxmlGetFirstChild(layer_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("layer", name) == 0)
			{
				layer_names[i] = mxmlElementGetAttr(temp_node, "name");
				int j = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						j++;
					}
				}
				i += (j / 17);
			}
		}
		num_textures = ReadFileU32BigEndian(tpl_fptr, 4);
		textures = malloc(num_textures * sizeof(AtbTexture));
		layer_data_offset = pattern_offset + (num_patterns * sizeof(AtbPatternData));
		bank_data_offset = layer_data_offset + (num_layers * sizeof(LayerData));
		frame_offset = bank_data_offset + (num_banks * sizeof(AtbBankData));
		texture_offset = frame_offset + (num_frames * sizeof(AnimFrame));
		texture_data_offset = texture_offset + (num_textures * sizeof(AtbTexture));
		texture_data_offset = (texture_data_offset + TEXTURE_ALIGNMENT - 1) & ~(TEXTURE_ALIGNMENT - 1);
		WriteFileU16BigEndian(out_fptr, offsetof(AtbFile, num_banks), num_banks);
		WriteFileU16BigEndian(out_fptr, offsetof(AtbFile, num_patterns), num_patterns);
		WriteFileU16BigEndian(out_fptr, offsetof(AtbFile, num_textures), num_textures);
		WriteFileU16BigEndian(out_fptr, offsetof(AtbFile, num_references), 0);
		WriteFileU32BigEndian(out_fptr, offsetof(AtbFile, bank_data_offset), bank_data_offset);
		WriteFileU32BigEndian(out_fptr, offsetof(AtbFile, pattern_data_offset), pattern_offset);
		WriteFileU32BigEndian(out_fptr, offsetof(AtbFile, texture_data_offset), texture_offset);
		temp_node = mxmlGetFirstChild(pattern_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("pattern", name) == 0)
			{
				mxml_node_t *node;
				mxml_node_t *child;
				node = mxmlGetFirstChild(temp_node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				patterns[i].num_layers = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				patterns[i].center_x = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				patterns[i].center_y = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				patterns[i].width = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);;
				child = mxmlGetFirstChild(node);
				patterns[i].height = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				patterns[i].first_layer_offset = layer_data_offset + (FindStringinArray(layer_names, mxmlGetText(child, NULL), num_layers) * sizeof(LayerData));
				i++;
			}
		}
		i = 0;
		for (i = 0; i < num_patterns; i++)
		{
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, num_layers), patterns[i].num_layers);
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, center_x), patterns[i].center_x);
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, center_y), patterns[i].center_y);
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, width), patterns[i].width);
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, height), patterns[i].height);
			WriteFileU16BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, pad), 0);
			WriteFileU32BigEndian(out_fptr, pattern_offset + (i * sizeof(AtbPatternData)) + offsetof(AtbPatternData, first_layer_offset), patterns[i].first_layer_offset);
		}
		temp_node = mxmlGetFirstChild(layer_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("layer", name) == 0)
			{
				layer_names[i] = mxmlElementGetAttr(temp_node, "name");
				int j = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						j++;
					}
				}
				int k;
				node = mxmlGetFirstChild(temp_node);
				node = mxmlGetNextSibling(node);
				for (k = 0; k < (j / 17); k++)
				{
					mxml_node_t *child;
					child = mxmlGetFirstChild(node);
					layers[i + k].alpha = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].flip = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].texture_index = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].texcoord_upper_left_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].texcoord_upper_left_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].texcoord_width = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].texcoord_height = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].shift_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].shift_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].upper_left_vertex_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].upper_left_vertex_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].upper_right_vertex_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].upper_right_vertex_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].lower_right_vertex_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].lower_right_vertex_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].lower_left_vertex_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					layers[i + k].lower_left_vertex_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
				}
				i += (j / 17);
			}
		}
		for (i = 0; i < num_layers; i++)
		{
			WriteFileU8(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, alpha), layers[i].alpha);
			WriteFileU8(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, flip), layers[i].flip);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, texture_index), layers[i].texture_index);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, texcoord_upper_left_x), layers[i].texcoord_upper_left_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, texcoord_upper_left_y), layers[i].texcoord_upper_left_y);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, texcoord_width), layers[i].texcoord_width);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, texcoord_height), layers[i].texcoord_height);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, shift_x), layers[i].shift_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, shift_y), layers[i].shift_y);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, upper_left_vertex_x), layers[i].upper_left_vertex_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, upper_left_vertex_y), layers[i].upper_left_vertex_y);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, upper_right_vertex_x), layers[i].upper_right_vertex_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, upper_right_vertex_y), layers[i].upper_right_vertex_y);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, lower_right_vertex_x), layers[i].lower_right_vertex_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, lower_right_vertex_y), layers[i].lower_right_vertex_y);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, lower_left_vertex_x), layers[i].lower_left_vertex_x);
			WriteFileU16BigEndian(out_fptr, layer_data_offset + (i * sizeof(LayerData)) + offsetof(LayerData, lower_left_vertex_y), layers[i].lower_left_vertex_y);
		}
		for (i = 0; i < num_textures; i++)
		{
			int image_src_offset = ReadFileU32BigEndian(tpl_fptr, sizeof(TplHeader) + (i * sizeof(TplImageDescription)) + offsetof(TplImageDescription, image_offset));
			int palette_src_offset = ReadFileU32BigEndian(tpl_fptr, sizeof(TplHeader) + (i * sizeof(TplImageDescription)) + offsetof(TplImageDescription, palette_offset));
			int gx_tex_format = ReadFileU32BigEndian(tpl_fptr, image_src_offset + offsetof(TplImageHeader, format));
			char atb_texture_format = GxtoAtbTextureFormat(gx_tex_format);
			int texture_bpp = GetTextureFormatBpp(gx_tex_format);
			short tex_width = ReadFileU16BigEndian(tpl_fptr, image_src_offset + offsetof(TplImageHeader, width));
			short tex_height = ReadFileU16BigEndian(tpl_fptr, image_src_offset + offsetof(TplImageHeader, height));
			int data_offset = ReadFileU32BigEndian(tpl_fptr, image_src_offset + offsetof(TplImageHeader, data_offset));
			short palette_len = 0;
			int palette_data_offset = 0;
			if (palette_src_offset != 0)
			{
				palette_len = ReadFileU16BigEndian(tpl_fptr, palette_src_offset + offsetof(TplPaletteHeader, num_entries));
				palette_data_offset = ReadFileU32BigEndian(tpl_fptr, palette_src_offset + offsetof(TplPaletteHeader, data_offset));
			}
			int bitmap_len;
			if (texture_bpp == 4)
			{
				bitmap_len = ((tex_width + 7) & ~7)*((tex_height + 7) & ~7)*texture_bpp / 8;
			}
			if (texture_bpp == 8)
			{
				bitmap_len = ((tex_width + 7) & ~7)*((tex_height + 3) & ~3)*texture_bpp / 8;
			}
			if (texture_bpp == 16||texture_bpp == 32)
			{
				bitmap_len = ((tex_width + 3) & ~3)*((tex_height + 3) & ~3)*texture_bpp / 8;
			}
			WriteFileU8(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, bpp), texture_bpp);
			WriteFileU8(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, format), atb_texture_format);
			WriteFileU16BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, palette_len), palette_len);
			WriteFileU16BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, width), tex_width);
			WriteFileU16BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, height), tex_height);
			WriteFileU32BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, bitmap_len), bitmap_len);
			WriteFileU32BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, palette_offset), texture_data_offset);
			if (palette_src_offset != 0)
			{
				ReadFileArray(tpl_fptr, palette, palette_data_offset, palette_len * 2);
				WriteFileArray(out_fptr, palette, texture_data_offset, palette_len * 2);
				texture_data_offset += (palette_len * 2);
				texture_data_offset = (texture_data_offset + TEXTURE_ALIGNMENT - 1) & ~(TEXTURE_ALIGNMENT - 1);
			}
			WriteFileU32BigEndian(out_fptr, texture_offset + (i * sizeof(AtbTexture)) + offsetof(AtbTexture, bitmap_offset), texture_data_offset);
			ReadFileArray(tpl_fptr, bitmap, data_offset, bitmap_len);
			WriteFileArray(out_fptr, bitmap, texture_data_offset, bitmap_len);
			texture_data_offset += bitmap_len;
			texture_data_offset = (texture_data_offset + TEXTURE_ALIGNMENT - 1) & ~(TEXTURE_ALIGNMENT - 1);
		}
		temp_node = mxmlGetFirstChild(bank_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("bank", name) == 0)
			{
				mxml_node_t *node;
				mxml_node_t *child;
				node = mxmlGetFirstChild(temp_node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				banks[i].frame_count = atoi(mxmlGetText(child, NULL));
				node = mxmlGetNextSibling(node);
				node = mxmlGetNextSibling(node);
				child = mxmlGetFirstChild(node);
				banks[i].first_anim_frame_offset = frame_offset + (FindStringinArray(frame_names, mxmlGetText(child, NULL), num_frames) * sizeof(AnimFrame));
				i++;
			}
		}
		for (i = 0; i < num_banks; i++)
		{
			WriteFileU16BigEndian(out_fptr, bank_data_offset + (i * sizeof(AtbBankData)) + offsetof(AtbBankData, frame_count), banks[i].frame_count);
			WriteFileU16BigEndian(out_fptr, bank_data_offset + (i * sizeof(AtbBankData)) + offsetof(AtbBankData, pad), 0);
			WriteFileU32BigEndian(out_fptr, bank_data_offset + (i * sizeof(AtbBankData)) + offsetof(AtbBankData, first_anim_frame_offset), banks[i].first_anim_frame_offset);
		}
		temp_node = mxmlGetFirstChild(frame_node);
		i = 0;
		while (temp_node)
		{
			temp_node = mxmlGetNextSibling(temp_node);
			char *name = mxmlGetElement(temp_node);
			if (name != NULL && strcmp("frame", name) == 0)
			{
				frame_names[i] = mxmlElementGetAttr(temp_node, "name");
				int j = 0;
				mxml_node_t *node = mxmlGetFirstChild(temp_node);
				while (node)
				{
					node = mxmlGetNextSibling(node);
					char *node_name = mxmlGetElement(node);
					if (node_name != NULL)
					{
						j++;
					}
				}
				int k;
				node = mxmlGetFirstChild(temp_node);
				node = mxmlGetNextSibling(node);
				for (k = 0; k < (j / 5); k++)
				{
					mxml_node_t *child;
					child = mxmlGetFirstChild(node);
					char *text = mxmlGetText(child, NULL);
					if (text == NULL || strcmp(text, "NULL") == 0)
					{
						frames[i + k].pattern_index = 0;
					}
					else
					{
						frames[i + k].pattern_index = FindStringinArray(pattern_names, text, num_patterns);
					}
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					frames[i + k].frame_length = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					frames[i + k].shift_x = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					frames[i + k].shift_y = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
					child = mxmlGetFirstChild(node);
					frames[i + k].flip = atoi(mxmlGetText(child, NULL));
					node = mxmlGetNextSibling(node);
					node = mxmlGetNextSibling(node);
				}
				i += (j / 5);
			}
		}
		i = 0;
		for (i = 0; i < num_frames; i++)
		{
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, pattern_index), frames[i].pattern_index);
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, frame_length), frames[i].frame_length);
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, shift_x), frames[i].shift_x);
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, shift_y), frames[i].shift_y);
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, flip), frames[i].flip);
			WriteFileU16BigEndian(out_fptr, frame_offset + (i * sizeof(AnimFrame)) + offsetof(AnimFrame, unk), 0);
		}
		fclose(out_fptr);
		fclose(tpl_fptr);
		return 1;
	}
	else
	{
		printf("Usage is atbpack out_file in_tpl in_xml.\n");
		getchar();
		return 0;
	}
}

int GetTextureFormatBpp(int gx_format)
{
	switch (gx_format)
	{
	case GX_TEXTURE_FORMAT_RGBA8:
		return 32;

	case GX_TEXTURE_FORMAT_RGB5A3:
		return 16;

	case GX_TEXTURE_FORMAT_CI8:
		return 8;

	case GX_TEXTURE_FORMAT_CI4:
		return 4;

	case GX_TEXTURE_FORMAT_IA8:
		return 16;

	case GX_TEXTURE_FORMAT_IA4:
		return 8;

	case GX_TEXTURE_FORMAT_I8:
		return 8;

	case GX_TEXTURE_FORMAT_I4:
		return 4;

	case GX_TEXTURE_FORMAT_A8:
		return 8;

	case GX_TEXTURE_FORMAT_CMPR:
		return 4;

	default:
		return 4;
	}
}
int GxtoAtbTextureFormat(int gx_format)
{
	switch (gx_format)
	{
		case GX_TEXTURE_FORMAT_RGBA8:
			return ATB_TEXTURE_FORMAT_RGBA8;

		case GX_TEXTURE_FORMAT_RGB5A3:
			return ATB_TEXTURE_FORMAT_RGB5A3;

		case GX_TEXTURE_FORMAT_CI8:
			return ATB_TEXTURE_FORMAT_CI8;

		case GX_TEXTURE_FORMAT_CI4:
			return ATB_TEXTURE_FORMAT_CI4;

		case GX_TEXTURE_FORMAT_IA8:
			return ATB_TEXTURE_FORMAT_IA8;

		case GX_TEXTURE_FORMAT_IA4:
			return ATB_TEXTURE_FORMAT_IA4;

		case GX_TEXTURE_FORMAT_I8:
			return ATB_TEXTURE_FORMAT_I8;

		case GX_TEXTURE_FORMAT_I4:
			return ATB_TEXTURE_FORMAT_I4;

		case GX_TEXTURE_FORMAT_A8:
			return ATB_TEXTURE_FORMAT_A8;

		case GX_TEXTURE_FORMAT_CMPR:
			return ATB_TEXTURE_FORMAT_CMPR;

		default:
			return 0;
	}
}

int FindStringinArray(char **str_array, char *str, int array_entries)
{
	int i;
	for (i = 0; i < array_entries; i++)
	{
		if (str_array[i] != NULL && strcmp(str_array[i], str) == 0)
		{
			return i;
		}
	}
	return -1;
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