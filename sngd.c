/*****************************************************************************

NAME
   sngle.c -- decompile PNG to SNG.

*****************************************************************************/
#include <stdlib.h>
#include <stdarg.h>
#define PNG_INTERNAL
#include "config.h"	/* for RGBTXT */
#include "png.h"
#include "sng.h"

static char *image_type[] = {
    "grayscale",
    "undefined type",
    "RGB",
    "colormap",
    "grayscale+alpha",
    "undefined type",
    "RGB+alpha"
};

static char *rendering_intent[] = {
  "perceptual",
  "relative colorimetric",
  "saturation-preserving",
  "absolute colorimetric"
};

static char *current_file;
static png_structp png_ptr;
static png_infop info_ptr;
static int true_depth, true_channels;

/*****************************************************************************
 *
 * Interface to RGB database
 *
 *****************************************************************************/

#define COLOR_HASH_MODULUS	137		/* should be prime */
#define COLOR_HASH(r, g, b)	(((r<<16)|(g<<8)|(b))%COLOR_HASH_MODULUS)

static color_item *rgb_hashbuckets[COLOR_HASH_MODULUS];
static int rgb_initialized;

static int hash_by_rgb(color_item *cp)
/* hash by color's RGB value */
{
    return(COLOR_HASH(cp->r, cp->g, cp->b));
}

static char *find_by_rgb(int r, int g, int b)
{
    color_item sc, *hp;

   sc.r = r; sc.g = g; sc.b = b;

    for (hp = rgb_hashbuckets[hash_by_rgb(&sc)]; hp; hp = hp->next)
	if (hp->r == r && hp->g == g && hp->b == b)
	    return(hp->name);

    return((char *)NULL);
}

/*****************************************************************************
 *
 * Low-level helper code
 *
 *****************************************************************************/

char *safeprint(const char *buf)
/* visibilize a given string -- inverse of sngc.c:escapes() */
{
    static char vbuf[PNG_STRING_MAX_LENGTH*4+1];
    char *tp = vbuf;

    while (*buf)
    {
	if (*buf == '"')
	{
	    *tp++ = '\\'; *tp++ = '"';
	    buf++;
	}
	else if (*buf == '\\')
	{
	    *tp++ = '\\'; *tp++ = '\\';
	    buf++;
	}
	else if (isprint(*buf) || *buf == ' ')
	    *tp++ = *buf++;
	else if (*buf == '\n')
	{
	    *tp++ = '\\'; *tp++ = 'n';
	    buf++;
	}
	else if (*buf == '\r')
	{
	    *tp++ = '\\'; *tp++ = 'r';
	    buf++;
	}
	else if (*buf == '\b')
	{
	    *tp++ = '\\'; *tp++ = 'b';
	    buf++;
	}
	else if (*buf < ' ')
	{
	    *tp++ = '\\'; *tp++ = '^'; *tp++ = '@' + *buf;
	    buf++;
	}
	else
	{
	    (void) sprintf(tp, "\\0x%02x", *buf++);
	    tp += strlen(tp);
	}
    }
    *tp++ = '\0';
    return(vbuf);
}

static void multi_dump(FILE *fpout, char *leader,
		       int width, int height,
		       unsigned char *data[])
/* dump data in a recompilable form */
{
    unsigned char *cp;
    int i, all_printable = 1, base64 = 1;

#define SHORT_DATA	50

    for (i = 0; i < height; i++)
	for (cp = data[i]; cp < data[i] + width; cp++)
	{
	    if (!isprint(*cp) && !isspace(*cp))
		all_printable = 0;
	    if (*cp > 64)
		base64 = 0;
	}

    for (i = 0; i < height; i++)
    {
	if (all_printable)
	{
	    unsigned char *cp;

	    if (i == 0)
	    {
		fprintf(fpout, "%s ", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }

	    fputc('"', fpout);
	    for (cp = data[i]; cp < data[i] + width; cp++)
	    {
		char	cbuf[2];

		cbuf[0] = *cp;
		cbuf[1] = '\0';
		fputs(safeprint(cbuf), fpout); 

		if (*cp == '\n' && cp < data[i] + width - 1)
		    fprintf(fpout, "\"\n\"");
	    }
	    fprintf(fpout, "\"%c\n", height == 1 ? ';' : ' ');
	}
	else if (base64)
	{
	    if (i == 0)
	    {
		fprintf(fpout, "%sbase64", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }
	    for (cp = data[i]; cp < data[i] + width; cp++)
		fputc(BASE64[*cp], fpout);
	    if (height == 1)
		fprintf(fpout, ";\n");
	    else
		fprintf(fpout, "\n");
	}
	else
	{
	    if (i == 0)
	    {
		fprintf(fpout, "%shex", leader);
		if (height == 1 && width < SHORT_DATA)
		    fprintf(fpout, " ");
		else
		    fprintf(fpout, "\n");
	    }
	    for (cp = data[i]; cp < data[i] + width; cp++)
	    {
		fprintf(fpout, "%02x", *cp & 0xff);

		/* only insert spacers for 8-bit images if > 1 channel */
		if (true_depth == 8 && true_channels > 1)
		{
		    if (((cp - data[i]) % true_channels) == true_channels - 1)
			fputc(' ', fpout);
		}
		else if (true_depth == 16)
		    if (((cp - data[i]) % (true_channels*2)) == true_channels*2-1)
			fputc(' ', fpout);
	    }
	    if (height == 1)
		fprintf(fpout, ";\n");
	    else
		fprintf(fpout, "\n");
	}
    }
}

static void dump_data(FILE *fpout, char *leader, int size, unsigned char *data)
{
    unsigned char *dope[1];

    dope[0] = data;
    multi_dump(fpout, leader, size, 1, dope);
}

static void printerr(int err, const char *fmt, ... )
/* throw an error distinguishable from PNG library errors */
{
    char buf[BUFSIZ];
    va_list ap;

    sprintf(buf, "sng: in %s, ", current_file);

    va_start(ap, fmt);
    vsprintf(buf + strlen(buf), fmt, ap);
    va_end(ap);

    strcat(buf, "\n");
    fputs(buf, stderr);

    sng_error = err;
}

/*****************************************************************************
 *
 * Chunk handlers
 *
 *****************************************************************************/

static void dump_IHDR(FILE *fpout)
{
    int ityp;

    if (info_ptr->width == 0 || info_ptr->height == 0) {
	printerr(1, "invalid IHDR image dimensions (%lux%lu)",
		 info_ptr->width, info_ptr->height);
    }

    ityp = info_ptr->color_type;
    if (ityp > sizeof(image_type)/sizeof(char*)) {
	ityp = 1; /* avoid out of range array index */
    }
    switch (info_ptr->bit_depth) {
    case 1:
    case 2:
    case 4:
	if (ityp == 2 || ityp == 4 || ityp == 6) {/* RGB or GA or RGBA */
	    printerr(1, "invalid IHDR bit depth (%u) for %s image",
		     true_depth, image_type[ityp]);
	}
	break;
    case 8:
	break;
    case 16:
	if (ityp == 3) { /* palette */
	    printerr(1, "invalid IHDR bit depth (%u) for %s image",
		     true_depth, image_type[ityp]);
	}
	break;
    default:
	printerr(1, "invalid IHDR bit depth (%u)", info_ptr->bit_depth);
	break;
    }

    fprintf(fpout, "IHDR {\n");
    fprintf(fpout, "    width: %lu; height: %lu; bitdepth: %u;\n", 
	    info_ptr->width, info_ptr->height, true_depth);
    fprintf(fpout, "    using");
    if (ityp & PNG_COLOR_MASK_COLOR)
	fprintf(fpout, " color");
    else
	fprintf(fpout, " grayscale");
    if (ityp & PNG_COLOR_MASK_PALETTE)
	fprintf(fpout, " palette");
    if (ityp & PNG_COLOR_MASK_ALPHA)
	fprintf(fpout, " alpha");
    fprintf(fpout, ";\n");
    if (info_ptr->interlace_type)
	fprintf(fpout, "    with interlace;        # type adam7 assumed\n");
    fprintf(fpout, "}\n");
}

static void dump_PLTE(FILE *fpout)
{
    int i;

    initialize_hash(hash_by_rgb, rgb_hashbuckets, &rgb_initialized);

    if (info_ptr->color_type & PNG_COLOR_MASK_PALETTE)
    {
	fprintf(fpout, "PLTE {\n");
	for (i = 0;  i < info_ptr->num_palette;  i++)
	{
	    char	*name = NULL;

	    fprintf(fpout, 
		    "    (%3u,%3u,%3u)     # rgb = (0x%02x,0x%02x,0x%02x)",
		    info_ptr->palette[i].red,
		    info_ptr->palette[i].green,
		    info_ptr->palette[i].blue,
		    info_ptr->palette[i].red,
		    info_ptr->palette[i].green,
		    info_ptr->palette[i].blue);

	    if (rgb_initialized)
		name = find_by_rgb(info_ptr->palette[i].red,
				   info_ptr->palette[i].green,
				   info_ptr->palette[i].blue);
	    if (name)
		fprintf(fpout, " %s", name);
	    fputc('\n', fpout);
	}

	fprintf(fpout, "}\n");
    }
}

static void dump_image(png_bytepp rows, FILE *fpout)
{
#ifdef __UNUSED__
    if (idat)
    {
	int	i;

	for (i = 0; i < idat; i++)
	{
	    fprintf(fpout, "IDAT {\n");
	    fprintf(fpout, "}\n");
	}
    }
    else
#endif /* __UNUSED__ */
    {
	fprintf(fpout, "IMAGE {\n");
	multi_dump(fpout, "    ", 
		   png_get_rowbytes(png_ptr, info_ptr),  info_ptr->height,
		   rows);
	fprintf(fpout, "}\n");
    }
}

static void dump_bKGD(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_bKGD)
    {
	fprintf(fpout, "bKGD {");
	switch (info_ptr->color_type) {
	case PNG_COLOR_TYPE_GRAY:
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	    fprintf(fpout, "gray: %u;", info_ptr->background.gray);
	    break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    fprintf(fpout, "red: %u;  green: %u;  blue: %u;",
			info_ptr->background.red,
			info_ptr->background.green,
			info_ptr->background.blue);
	    break;
	case PNG_COLOR_TYPE_PALETTE:
	    fprintf(fpout, "index: %u", info_ptr->background.index);
	    break;
	default:
	    printerr(1, "unknown image type");
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_cHRM(FILE *fpout)
{
    double wx, wy, rx, ry, gx, gy, bx, by;

    wx = info_ptr->x_white;
    wy = info_ptr->y_white;
    rx = info_ptr->x_red;
    ry = info_ptr->y_red;
    gx = info_ptr->x_green;
    gy = info_ptr->y_green;
    bx = info_ptr->x_blue;
    by = info_ptr->y_blue;

    if (wx < 0 || wx > 0.8 || wy < 0 || wy > 0.8 || wx + wy > 1.0) {
	printerr(1, "invalid cHRM white point %0g %0g", wx, wy);
    } else if (rx < 0 || rx > 0.8 || ry < 0 || ry > 0.8 || rx + ry > 1.0) {
	printerr(1, "invalid cHRM red point %0g %0g", rx, ry);
    } else if (gx < 0 || gx > 0.8 || gy < 0 || gy > 0.8 || gx + gy > 1.0) {
	printerr(1, "invalid cHRM green point %0g %0g", gx, gy);
    } else if (bx < 0 || bx > 0.8 || by < 0 || by > 0.8 || bx + by > 1.0) {
	printerr(1, "invalid cHRM blue point %0g %0g", bx, by);
    }

    if (info_ptr->valid & PNG_INFO_cHRM) {
	fprintf(fpout, "cHRM {\n");
	fprintf(fpout, "    white: (%0g, %0g);\n", wx, wy);
	fprintf(fpout, "    red:   (%0g, %0g);\n", rx, ry);
	fprintf(fpout, "    green: (%0g, %0g);\n", gx, gy);
	fprintf(fpout, "    blue:  (%0g, %0g);\n", bx, by);
	fprintf(fpout, "}\n");
    }
}

static void dump_gAMA(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_gAMA) {
        fprintf(fpout, "gAMA {%#0.5g}\n", info_ptr->gamma);
    }
}

static void dump_hIST(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_hIST) {
	int	j;

	fprintf(fpout, "hIST {\n");
	fprintf(fpout, "   ");
	for (j = 0; j < info_ptr->num_palette;  j++)
	    fprintf(fpout, " %3u", info_ptr->hist[j]);
	fprintf(fpout, ";\n}\n");
    }
}

static void dump_iCCP(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_iCCP) {
	fprintf(fpout, "iCCP {\n");
	fprintf(fpout, "    name: \"%s\"\n", safeprint(info_ptr->iccp_name));
	dump_data(fpout, "    profile: ", 
		  info_ptr->iccp_proflen, info_ptr->iccp_profile);
	fprintf(fpout, "}\n");
    }
}

static void dump_oFFs(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_oFFs) {
        fprintf(fpout, "oFFs {xoffset: %ld; yoffset: %ld;",
	       info_ptr->x_offset, info_ptr->y_offset);
	if (info_ptr->offset_unit_type == PNG_OFFSET_PIXEL)
	    fprintf(fpout, " unit: pixels");
	else if (info_ptr->offset_unit_type == PNG_OFFSET_MICROMETER)
	    fprintf(fpout, " unit: micrometers");
	fprintf(fpout, ";}\n");
    }
}
static void dump_pHYs(FILE *fpout)
{
    if (info_ptr->phys_unit_type > 1)
        printerr(1, "invalid pHYs unit");
    else if (info_ptr->valid & PNG_INFO_pHYs) {
	fprintf(fpout, "");
        fprintf(fpout, "pHYs {xpixels: %lu; ypixels: %lu;",
	       info_ptr->x_pixels_per_unit, info_ptr->y_pixels_per_unit);
	if (info_ptr->phys_unit_type == PNG_RESOLUTION_METER)
	    fprintf(fpout, " per: meter;");
        fprintf(fpout, "}");
        if (info_ptr->phys_unit_type == 1 && info_ptr->x_pixels_per_unit == info_ptr->y_pixels_per_unit)
	    fprintf(fpout, "  # (%lu dpi)\n", (long)(info_ptr->x_pixels_per_unit*0.0254 + 0.5));
	else
	    fputc('\n', fpout);
    }
}

static void dump_sBIT(FILE *fpout)
{
    int maxbits = (info_ptr->color_type == 3)? 8 : info_ptr->bit_depth;

    if (info_ptr->valid & PNG_INFO_pHYs) {
	fprintf(fpout, "sBIT {\n");
	switch (info_ptr->color_type) {
	case PNG_COLOR_TYPE_GRAY:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(1, "%u sBIT gray bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.gray, maxbits);
	    } else {
		fprintf(fpout, "    gray: %u;\n", info_ptr->sig_bit.gray);
	    }
	    break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_PALETTE:
	    if (info_ptr->sig_bit.red == 0 || info_ptr->sig_bit.red > maxbits) {
		printerr(1, "%u sBIT red bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.red, maxbits);
	    } else if (info_ptr->sig_bit.green == 0 || info_ptr->sig_bit.green > maxbits) {
		printerr(1, "%u sBIT green bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.green, maxbits);
	    } else if (info_ptr->sig_bit.blue == 0 || info_ptr->sig_bit.blue > maxbits) {
		printerr(1, "%u sBIT blue bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.blue, maxbits);
	    } else {
		fprintf(fpout, "    red: %u; green: %u; blue: %u;\n",
			info_ptr->sig_bit.red, info_ptr->sig_bit.green, info_ptr->sig_bit.blue);
	    }
	    break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(2, "%u sBIT gray bits not valid for %ubit/sample image\n",
			 info_ptr->sig_bit.gray, maxbits);
	    } else if (info_ptr->sig_bit.alpha == 0 || info_ptr->sig_bit.alpha > maxbits) {
		printerr(2, "%u sBIT alpha bits(tm) not valid for %ubit/sample image\n",
			 info_ptr->sig_bit.alpha, maxbits);
	    } else {
		fprintf(fpout, "    gray: %u; alpha: %u\n", info_ptr->sig_bit.gray, info_ptr->sig_bit.alpha);
	    }
	    break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(1, "%u sBIT red bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.gray, maxbits);
	    } else if (info_ptr->sig_bit.green == 0 || info_ptr->sig_bit.green > maxbits) {
		printerr(1, "%u sBIT green bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.green, maxbits);
	    } else if (info_ptr->sig_bit.blue == 0 || info_ptr->sig_bit.blue > maxbits) {
		printerr(1, "%u sBIT blue bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.blue, maxbits);
	    } else if (info_ptr->sig_bit.alpha == 0 || info_ptr->sig_bit.alpha > maxbits) {
		printerr(1, "%u sBIT alpha bits not valid for %ubit/sample image",
			 info_ptr->sig_bit.alpha, maxbits);
	    } else {
		fprintf(fpout, "    red: %u; green: %u; blue: %u; alpha: %u;\n",
			info_ptr->sig_bit.red, 
			info_ptr->sig_bit.green,
			info_ptr->sig_bit.blue,
			info_ptr->sig_bit.alpha);
	    }
	    break;
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_pCAL(FILE *fpout)
{
    static char *mapping_type[] = {
	"linear", "euler", "exponential", "hyperbolic"
    }; 

    if (info_ptr->pcal_type >= PNG_EQUATION_LAST)
	    printerr(1, "invalid equation type in pCAL");
    else if (info_ptr->valid & PNG_INFO_pCAL) {
	int	i;

	fprintf(fpout, "pCAL {\n");
	fprintf(fpout, "    name: \"%s\";\n", safeprint(info_ptr->pcal_purpose));
	fprintf(fpout, "    x0: %ld;\n", info_ptr->pcal_X0);
	fprintf(fpout, "    x1: %ld;\n", info_ptr->pcal_X1);
	fprintf(fpout, "    mapping: %s;        # equation type %u\n", 
	       mapping_type[info_ptr->pcal_type], info_ptr->pcal_type);
	fprintf(fpout, "    unit: \"%s\"\n", safeprint(info_ptr->pcal_units));
	if (info_ptr->pcal_nparams)
	{
	    fprintf(fpout, "    parameters:");
	    for (i = 0; i < info_ptr->pcal_nparams; i++)
		fprintf(fpout, " %s", safeprint(info_ptr->pcal_params[i]));
	    fprintf(fpout, ";\n");
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_sCAL(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_sCAL) {
	fprintf(fpout, "sCAL {\n");
	switch (info_ptr->scal_unit)
	{
	case PNG_SCALE_METER:
	    fprintf(fpout, "    unit:   meter\n");
	    break;
	case PNG_SCALE_RADIAN:
	    fprintf(fpout, "    unit:   radian\n");
	    break;
	default:
	    fprintf(fpout, "    unit:   unknown\n");
	    break;
	}
	fprintf(fpout, "    width:  %g\n", info_ptr->scal_pixel_width);
	fprintf(fpout, "    height: %g\n", info_ptr->scal_pixel_height);
	fprintf(fpout, "}\n");
    }
}

static void dump_sPLT(png_spalette *ep, FILE *fpout)
{
    long i;

    initialize_hash(hash_by_rgb, rgb_hashbuckets, &rgb_initialized);

    fprintf(fpout, "sPLT {\n");
    fprintf(fpout, "    name: \"%s\";\n", safeprint(ep->name));
    fprintf(fpout, "    depth: %u;\n", ep->depth);

    for (i = 0;  i < ep->nentries;  i++)
    {
	char *name;

	fprintf(fpout, "    (%3u,%3u,%3u,%3u,%u) "
		"    # rgba = (0x%02x,0x%02x,0x%02x,0x%02x)",
		ep->entries[i].red,
		ep->entries[i].green,
		ep->entries[i].blue,
		ep->entries[i].alpha,
		ep->entries[i].frequency,
		ep->entries[i].red,
		ep->entries[i].green,
		ep->entries[i].blue,
		ep->entries[i].alpha);

	if (rgb_initialized)
	    name = find_by_rgb(ep->entries[i].red,
			       ep->entries[i].green,
			       ep->entries[i].blue);
	if (name)
	    fprintf(fpout, " %s", name);

	fprintf(fpout, ", freq = %u\n", ep->entries[i].frequency);
    }

    fprintf(fpout, "}\n");
}

static void dump_tRNS(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_tRNS) {
	int	i;

	fprintf(fpout, "tRNS {\n");
	switch (info_ptr->color_type) {
	case PNG_COLOR_TYPE_GRAY:
	    fprintf(fpout, "    gray: %u;\n", info_ptr->trans_values.gray);
	    break;
	case PNG_COLOR_TYPE_RGB:
	    fprintf(fpout, "    red: %u; green: %u; blue: %u;\n",
		    info_ptr->trans_values.red,
		    info_ptr->trans_values.green,
		    info_ptr->trans_values.blue);
	    break;
	case PNG_COLOR_TYPE_PALETTE:
	    for (i = 0; i < info_ptr->num_trans; i++)
		fprintf(fpout, " %u", info_ptr->trans[i]);
	    break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
	case PNG_COLOR_TYPE_RGB_ALPHA:
	    printerr(1, "tRNS chunk illegal with this image type");
	    break;
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_sRGB(FILE *fpout)
{
    if (info_ptr->srgb_intent) {
        printerr(1, "sRGB invalid rendering intent");
    }
    if (info_ptr->valid & PNG_INFO_sRGB) {
        fprintf(fpout, "sRGB {intent: %u;}             # %s\n", 
	       info_ptr->srgb_intent, 
	       rendering_intent[info_ptr->srgb_intent]);
    }
}

static void dump_tIME(FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_tIME) {
	static char *months[] =
	{"(undefined)", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	char *month;

	if (info_ptr->mod_time.month < 1 || info_ptr->mod_time.month > 12)
	    month = months[0];
	else
	    month = months[info_ptr->mod_time.month];

	fprintf(fpout, "tIME {\n");
	fprintf(fpout, "    # %2d %s %4d %02d:%02d:%02d GMT\n", 
		info_ptr->mod_time.day,
		month,
		info_ptr->mod_time.year,
		info_ptr->mod_time.hour,
		info_ptr->mod_time.minute,
		info_ptr->mod_time.second);
	fprintf(fpout, "    year:   %u\n", info_ptr->mod_time.year);
	fprintf(fpout, "    month:  %u\n", info_ptr->mod_time.month);
	fprintf(fpout, "    day:    %u\n", info_ptr->mod_time.day);
	fprintf(fpout, "    hour:   %u\n", info_ptr->mod_time.hour);
	fprintf(fpout, "    minute: %u\n", info_ptr->mod_time.minute);
	fprintf(fpout, "    second: %u\n", info_ptr->mod_time.second);
	fprintf(fpout, "}\n");
    }
}

static void dump_text(FILE *fpout)
{
    int	i;

    for (i = 0; i < info_ptr->num_text; i++)
    {
	switch (info_ptr->text[i].compression)
	{
	case PNG_TEXT_COMPRESSION_NONE:
	    fprintf(fpout, "tEXt {\n");
	    fprintf(fpout, "    keyword: \"%s\";\n", 
		    safeprint(info_ptr->text[i].key));
	    break;

	case PNG_TEXT_COMPRESSION_zTXt:
	    fprintf(fpout, "zTXt {\n");
	    fprintf(fpout, "    keyword: \"%s\";\n", 
		    safeprint(info_ptr->text[i].key));
	    break;

	case PNG_ITXT_COMPRESSION_NONE:
	case PNG_ITXT_COMPRESSION_zTXt:
	    fprintf(fpout, "iTXt {\n");
	    fprintf(fpout, "    language: \"%s\";\n", 
		    safeprint(info_ptr->text[i].lang));
	    fprintf(fpout, "    keyword: \"%s\";\n", 
		    safeprint(info_ptr->text[i].key));
	    fprintf(fpout, "    translated: \"%s\";\n", 
		    safeprint(info_ptr->text[i].lang_key));
	    break;
	}

	fprintf(fpout, "    text: \"%s\";\n", 
		safeprint(info_ptr->text[i].text));
	fprintf(fpout, "}\n");
    }
}

static void dump_unknown_chunks(
				int after_idat, FILE *fpout)
{
    int	i;

    for (i = 0; i < info_ptr->unknown_chunks_num; i++)
    {
	png_unknown_chunk	*up = info_ptr->unknown_chunks + i;

	/* are we before or after the IDAT part? */
	if (after_idat != !!(up->location & PNG_HAVE_IDAT))
	    continue;

/* macros to extract big-endian short and long ints */
#define SH(p) ((unsigned short)((p)[1]) | (((p)[0]) << 8))
#define LG(p) ((unsigned long)(SH((p)+2)) | ((ulg)(SH(p)) << 16))

	if (!strcmp(up->name, "gIFg"))
	{
	    fprintf(fpout, "gIFg {\n", up->name);
	    fprintf(fpout, "    disposal: %d; input: %d; delay %f;\n",
		    up->data[0], up->data[1], (float)(.01 * SH(up->data+2)));
	}
	else if (!strcmp(up->name, "gIFx"))
	{
	    fprintf(fpout, "gIFx {\n", up->name);
	    fprintf(fpout, "    identifier: \"%.*s\"; code: \"%c%c%c\"\n",
		    8, up->data, up->data[8], up->data[9], up->data[10]);
	    dump_data(fpout, "    data: ", up->size - 11, up->data + 11);
	}
	else
	{
	    fprintf(fpout, "private %s {\n", up->name);
	    dump_data(fpout, "    ", up->size, up->data);
	}
	fprintf(fpout, "}\n");
#undef SH
#undef LG
    }
}

/*****************************************************************************
 *
 * Compiler main sequence
 *
 *****************************************************************************/

void sngdump(png_byte *row_pointers[], FILE *fpout)
/* dump a canonicalized SNG form of a PNG file */
{
    int	i;

    fprintf(fpout, "#SNG: from %s\n", current_file);

    dump_IHDR(fpout);			/* first critical chunk */

    dump_cHRM(fpout);
    dump_gAMA(fpout);
    dump_iCCP(fpout);
    dump_sBIT(fpout);
    dump_sRGB(fpout);

    dump_PLTE(fpout);			/* second critical chunk */

    dump_bKGD(fpout);
    dump_hIST(fpout);
    dump_tRNS(fpout);
    dump_pHYs(fpout);
    for (i = 0; i < info_ptr->splt_palettes_num; i++)
	dump_sPLT(info_ptr->splt_palettes + i, fpout);

    dump_unknown_chunks(FALSE, fpout);

    /*
     * This is the earliest point at which we could write the image data;
     * the ancillary chunks after this point have no order contraints.
     * We choose to write the image last so that viewers/editors can get
     * a look at all the ancillary.
     */

    dump_tIME(fpout);
    dump_text(fpout);

    dump_image(row_pointers, fpout);	/* third critical chunk */

    dump_unknown_chunks(TRUE, fpout);
}

int sngd(FILE *fp, char *name, FILE *fpout)
/* read and decompile an SNG image presented on stdin */
{
   png_uint_32 width, height;
   int bit_depth, color_type, interlace_type, row;
   png_bytepp row_pointers;

   current_file = name;
   sng_error = 0;

   /* Create and initialize the png_struct with the desired error handler
    * functions.  If you want to use the default stderr and longjump method,
    * you can supply NULL for the last three parameters.  We also supply the
    * the compiler header file version, so that we know if the application
    * was compiled with a compatible version of the library.  REQUIRED
    */
   png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

   if (png_ptr == NULL)
   {
      fclose(fp);
      sng_error = 1;
      return(1);
   }

   /* Allocate/initialize the memory for image information.  REQUIRED. */
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL)
   {
      fclose(fp);
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      sng_error = 1;
      return(FAIL);
   }

   /* Set error handling if you are using the setjmp/longjmp method (this is
    * the normal method of doing things with libpng).  REQUIRED unless you
    * set up your own error handlers in the png_create_read_struct() earlier.
    */
   if (setjmp(png_ptr->jmpbuf))
   {
      /* Free all of the memory associated with the png_ptr and info_ptr */
      png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
      fclose(fp);
      /* If we get here, we had a problem reading the file */
      sng_error = 1;
      return(1);
   }

   /* keep all unknown chunks, we'll dump them later */
   png_set_keep_unknown_chunks(png_ptr, 2, NULL, 0);

   /* Set up the input control if you are using standard C streams */
   png_init_io(png_ptr, fp);

   /* If we have already read some of the signature */
   /* png_set_sig_bytes(png_ptr, sig_read); */

#ifdef __UNUSED__
   if (idat)
       /* turn off IDAT chunk processing */;
#endif /* __UNUSED__ */

   /* The call to png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk).  REQUIRED
    */
   png_read_info(png_ptr, info_ptr);

   png_get_IHDR(png_ptr, info_ptr, 
		&width, &height, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

   /* need to gather some statistics before transformation */ 
   true_channels = png_get_channels(png_ptr, info_ptr);
   true_depth = bit_depth;

   if (bit_depth < 8)
   {
       png_set_packing(png_ptr);
       true_depth = bit_depth;
   }
   png_read_update_info(png_ptr, info_ptr);

#ifdef __UNUSED__
   /*
    * If idat is off, it's time to read the actual image data.
    * (If idat is on, it has been treated as unknown chunks.)
    */
   if (!idat)		/* read and dump as a sequence of IDATs */
   {
#endif /* __UNUSED__ */
       row_pointers = (png_bytepp)malloc(height * sizeof(png_bytep));
       for (row = 0; row < height; row++)
	   row_pointers[row] = malloc(png_get_rowbytes(png_ptr, info_ptr));

       png_read_image(png_ptr, row_pointers);
#ifdef __UNUSED__
   }
#endif /* __UNUSED__ */

   /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
   png_read_end(png_ptr, info_ptr);

   /* dump the image */
   sngdump(row_pointers, fpout);

   /* clean up after the read, and free any memory allocated - REQUIRED */
   png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

   /* close the file */
   fclose(fp);

   /* that's it */
   return(0);
}

/* sngd.c ends here */
