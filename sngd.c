#include <stdlib.h>
#include <stdarg.h>
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

static char *safeprint(char *str)
{
    return(str);
}

static void dump_data(FILE *fpout, 
		      char *leader, int size, unsigned char *data, int rowsize)
/* dump data in a recompilable form */
{
    unsigned char *cp;
    int all_printable = 1, base64 = 1;

#define SHORT_DATA	60

    for (cp = data; cp < data + size; cp++)
	if (!isprint(*cp) || *cp == '\n')
	    all_printable = 0;
	else if (*cp > 63)
	    base64 = 0;

    if (all_printable)
    {
	char *str = malloc(size + 1);

	memcpy(str, data, size);
	data[size] = '\0';
	fprintf(fpout, "%sstring \"%s\";\n", leader, data, str);
	free(str);
    }
    else if (base64)
    {
	fprintf(fpout, "%sbase64", leader);
	if (size < SHORT_DATA)
	    fprintf(fpout, " ");
	else
	    fprintf(fpout, "\n    ");
	for (cp = data; cp < data + size; cp++)
	{
	    if (rowsize && (cp - data) && ((cp - data) % rowsize))
		fprintf(fpout, "\n    ");
	    fputc("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ%$"[*cp], fpout);
	}
	fprintf(fpout, ";\n");
    }
    else
    {
	fprintf(fpout, "%shex", leader);
	if (size < SHORT_DATA)
	    fprintf(fpout, " ");
	else
	    fprintf(fpout, "\n    ");
	for (cp = data; cp < data + size; cp++)
	{
	    if (rowsize && (cp - data) && ((cp - data) % rowsize))
		fprintf(fpout, "\n    ");
	    fprintf(fpout, "%02x", *cp);
	}
	fprintf(fpout, ";\n");
    }
}

static void printerr(int err, const char *fmt, ... )
/* throw an error distinguishable from PNG library errors */
{
    char buf[BUFSIZ];
    va_list ap;

    /* error message format can be stepped through by Emacs */
    if (verbose)
	sprintf(buf, "%s ", current_file);
    else
	buf[0] = '\0';

    va_start(ap, fmt);
    vsprintf(buf + strlen(buf), fmt, ap);
    va_end(ap);

    strcat(buf, "\n");
    fputs(buf, stderr);

    sng_error = err;
}

static void dump_IHDR(png_infop info_ptr, FILE *fpout)
{
    int ityp;

    if (info_ptr->width == 0 || info_ptr->height == 0) {
	printerr(1, "invalid IHDR image dimensions (%ldx%ld)",
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
	    printerr(1, "invalid IHDR bit depth (%d) for %s image",
		     info_ptr->bit_depth, image_type[ityp]);
	}
	break;
    case 8:
	break;
    case 16:
	if (ityp == 3) { /* palette */
	    printerr(1, "invalid IHDR bit depth (%d) for %s image",
		     info_ptr->bit_depth, image_type[ityp]);
	}
	break;
    default:
	printerr(1, "invalid IHDR bit depth (%d)", info_ptr->bit_depth);
	break;
    }

    fprintf(fpout, "IHDR {\n");
    fprintf(fpout, "    width: %ld; height: %ld; bitdepth: %d;\n", 
	    info_ptr->width, info_ptr->height, info_ptr->bit_depth);
    if (ityp & (PNG_COLOR_MASK_COLOR|PNG_COLOR_MASK_ALPHA|PNG_COLOR_MASK_PALETTE))
    {
	fprintf(fpout, "    using");
	if (ityp & PNG_COLOR_MASK_COLOR)
	    fprintf(fpout, " color");
	if (ityp & PNG_COLOR_MASK_PALETTE)
	    fprintf(fpout, " palette");
	if (ityp & PNG_COLOR_MASK_ALPHA)
	    fprintf(fpout, " alpha");
	fprintf(fpout, ";    # image type: %s\n", image_type[ityp]);
    }
    if (info_ptr->interlace_type)
	fprintf(fpout, "    using interlace;        # type adam7 assumed\n");
    fprintf(fpout, "}\n");
}

static void dump_PLTE(png_infop info_ptr, FILE *fpout)
{
    int i;

    if (info_ptr->color_type & PNG_COLOR_MASK_PALETTE)
    {
	fprintf(fpout, "PLTE {\n");
	for (i = 0;  i < info_ptr->num_palette;  i++)
	    fprintf(fpout, 
		    "    (%3u,%3u,%3u)     # rgb = (0x%02x,0x%02x,0x%02x)\n",
		    info_ptr->palette[i].red,
		    info_ptr->palette[i].green,
		    info_ptr->palette[i].blue,
		    info_ptr->palette[i].red,
		    info_ptr->palette[i].green,
		    info_ptr->palette[i].blue);

	fprintf(fpout, "}\n");
    }
}

static void dump_image(png_infop info_ptr, png_bytepp rows, FILE *fpout)
{
    /* FIXME: dump image */
}

static void dump_bKGD(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_bKGD)
    {
	fprintf(fpout, "bKGD {");
	switch (info_ptr->color_type) {
	case 0:
	case 4:
	    fprintf(fpout, "gray: %d;", info_ptr->background.gray);
	    break;
	case 2:
	case 6:
	    fprintf(fpout, "red: %d;  green: %d;  blue: %d;",
			info_ptr->background.red,
			info_ptr->background.green,
			info_ptr->background.blue);
	    break;
	case 3:
	    fprintf(fpout, "index: %d", info_ptr->background.index);
	    break;
	default:
	    printerr(1, "unknown image type");
	}
	fprintf(fpout, "}\n");
    }
}

static void dump_cHRM(png_infop info_ptr, FILE *fpout)
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

static void dump_gAMA(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_gAMA) {
        fprintf(fpout, "gAMA {%#0.5g}\n", info_ptr->gamma);
    }
}

static void dump_hIST(png_infop info_ptr, FILE *fpout)
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

static void dump_iCCP(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_iCCP) {
	fprintf(fpout, "iCCP {\n");
	fprintf(fpout, "    name: \"%s\"\n", safeprint(info_ptr->iccp_name));
	dump_data(fpout, "    profile: ", 
		  info_ptr->iccp_proflen, info_ptr->iccp_profile, 0);
	fprintf(fpout, "}\n");
    }
}

static void dump_oFFs(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->valid & PNG_INFO_oFFs) {
	fprintf(fpout, "oFFs {\n");
        fprintf(fpout, "    offset: (%ld, %ld)",
	       info_ptr->x_offset, info_ptr->y_offset);
	if (info_ptr->offset_unit_type == PNG_OFFSET_PIXEL)
	    fprintf(fpout, " pixels");
	else if (info_ptr->offset_unit_type == PNG_OFFSET_MICROMETER)
	    fprintf(fpout, " micrometers");
	fprintf(fpout, ";\n}\n");
    }
}

static void dump_pHYs(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->phys_unit_type > 1)
        printerr(1, "invalid pHYs unit");
    else if (info_ptr->valid & PNG_INFO_pHYs) {
	fprintf(fpout, "pHYs {\n");
        fprintf(fpout, "    resolution: (%ld, %ld)", 
	       info_ptr->x_pixels_per_unit, info_ptr->y_pixels_per_unit);
	if (info_ptr->phys_unit_type == PNG_RESOLUTION_METER)
	    fprintf(fpout, " per meter;");
        if (info_ptr->phys_unit_type == 1 && info_ptr->x_pixels_per_unit == info_ptr->y_pixels_per_unit)
	    fprintf(fpout, "  # (%ld dpi)", (long)(info_ptr->x_pixels_per_unit*0.0254 + 0.5));
        fprintf(fpout, "\n}\n");
    }
}

static void dump_sBIT(png_infop info_ptr, FILE *fpout)
{
    int maxbits = (info_ptr->color_type == 3)? 8 : info_ptr->bit_depth;

    if (info_ptr->valid & PNG_INFO_pHYs) {
	fprintf(fpout, "sBIT {\n");
	switch (info_ptr->color_type) {
	case 0:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(1, "%d sBIT gray bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.gray, maxbits);
	    } else {
		fprintf(fpout, "    gray: %d;\n", info_ptr->sig_bit.gray);
	    }
	    break;
	case 2:
	case 3:
	    if (info_ptr->sig_bit.red == 0 || info_ptr->sig_bit.red > maxbits) {
		printerr(1, "%d sBIT red bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.red, maxbits);
	    } else if (info_ptr->sig_bit.green == 0 || info_ptr->sig_bit.green > maxbits) {
		printerr(1, "%d sBIT green bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.green, maxbits);
	    } else if (info_ptr->sig_bit.blue == 0 || info_ptr->sig_bit.blue > maxbits) {
		printerr(1, "%d sBIT blue bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.blue, maxbits);
	    } else {
		fprintf(fpout, "    red: %d; green: %d; blue: %d;\n",
			info_ptr->sig_bit.red, info_ptr->sig_bit.green, info_ptr->sig_bit.blue);
	    }
	    break;
	case 4:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(2, "%d sBIT gray bits not valid for %dbit/sample image\n",
			 info_ptr->sig_bit.gray, maxbits);
	    } else if (info_ptr->sig_bit.alpha == 0 || info_ptr->sig_bit.alpha > maxbits) {
		printerr(2, "%d sBIT alpha bits(tm) not valid for %dbit/sample image\n",
			 info_ptr->sig_bit.alpha, maxbits);
	    } else {
		fprintf(fpout, "    gray: %d; alpha: %d\n", info_ptr->sig_bit.gray, info_ptr->sig_bit.alpha);
	    }
	    break;
	case 6:
	    if (info_ptr->sig_bit.gray == 0 || info_ptr->sig_bit.gray > maxbits) {
		printerr(1, "%d sBIT red bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.gray, maxbits);
	    } else if (info_ptr->sig_bit.green == 0 || info_ptr->sig_bit.green > maxbits) {
		printerr(1, "%d sBIT green bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.green, maxbits);
	    } else if (info_ptr->sig_bit.blue == 0 || info_ptr->sig_bit.blue > maxbits) {
		printerr(1, "%d sBIT blue bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.blue, maxbits);
	    } else if (info_ptr->sig_bit.alpha == 0 || info_ptr->sig_bit.alpha > maxbits) {
		printerr(1, "%d sBIT alpha bits not valid for %dbit/sample image",
			 info_ptr->sig_bit.alpha, maxbits);
	    } else {
		fprintf(fpout, "    red: %d; green: %d; blue: %d; alpha: %d;\n",
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

static void dump_pCAL(png_infop info_ptr, FILE *fpout)
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
	fprintf(fpout, "    %s mapping;         # equation type %d\n", 
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

static void dump_sCAL(png_infop info_ptr, FILE *fpout)
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

    fprintf(fpout, "sPLT {\n");
    fprintf(fpout, "    name: \"%s\";\n", safeprint(ep->name));
    fprintf(fpout, "    depth: %u;\n", ep->depth);

    for (i = 0;  i < ep->nentries;  i++)
	fprintf(fpout, "    (%3u,%3u,%3u,%3u,%u) "
		"    # rgba = (0x%02x,0x%02x,0x%02x,0x%02x), "
		"freq = %u\n",
		ep->entries[i].red,
		ep->entries[i].green,
		ep->entries[i].blue,
		ep->entries[i].alpha,
		ep->entries[i].frequency,
		ep->entries[i].red,
		ep->entries[i].green,
		ep->entries[i].blue,
		ep->entries[i].alpha,
		ep->entries[i].frequency);

    fprintf(fpout, "}\n");
}

static void dump_tRNS(png_infop info_ptr, FILE *fpout)
{
    /* FIXME: dump tRNS */
}

static void dump_sRGB(png_infop info_ptr, FILE *fpout)
{
    if (info_ptr->srgb_intent) {
        printerr(1, "sRGB invalid rendering intent");
    }
    if (info_ptr->valid & PNG_INFO_sRGB) {
        fprintf(fpout, "sRGB {intent: %d;}             # %s\n", 
	       info_ptr->srgb_intent, 
	       rendering_intent[info_ptr->srgb_intent]);
    }
}

static void dump_tIME(png_infop info_ptr, FILE *fpout)
{
    /* FIXME: dump tIME */
}

static void dump_text(png_infop info_ptr, FILE *fpout)
{
    int	i;

    for (i = 0; i < info_ptr->num_text; i++)
    {
	switch (info_ptr->text[i].compression)
	{
	case PNG_TEXT_COMPRESSION_NONE:
	    fprintf(fpout, "tEXt {\n");
	    break;

	case PNG_TEXT_COMPRESSION_zTXt:
	    fprintf(fpout, "zTXt {\n");
	    break;

	case PNG_ITXT_COMPRESSION_NONE:
	case PNG_ITXT_COMPRESSION_zTXt:
	    fprintf(fpout, "iTXt {\n");
	    fprintf(fpout, "    language: \"%s\";\n", 
		    safeprint(info_ptr->text[i].lang));
	    break;
	}

	fprintf(fpout, "    keyword: \"%s\";\n", 
		safeprint(info_ptr->text[i].key));
	fprintf(fpout, "    text: \"%s\";\n", 
		safeprint(info_ptr->text[i].text));
	fprintf(fpout, "}\n");
    }
}

void sngdump(png_infop info_ptr, png_byte *row_pointers[], FILE *fpout)
/* dump a canonicalized SNG form of a PNG file */
{
    int	i;

    dump_IHDR(info_ptr, fpout);		/* first critical chunk */

    dump_cHRM(info_ptr, fpout);
    dump_gAMA(info_ptr, fpout);
    dump_iCCP(info_ptr, fpout);
    dump_sBIT(info_ptr, fpout);
    dump_sRGB(info_ptr, fpout);

    dump_PLTE(info_ptr, fpout);		/* second critical chunk */

    dump_bKGD(info_ptr, fpout);
    dump_hIST(info_ptr, fpout);
    dump_tRNS(info_ptr, fpout);
    dump_pHYs(info_ptr, fpout);
    for (i = 0; i < info_ptr->splt_palettes_num; i++)
	dump_sPLT(info_ptr->splt_palettes + i, fpout);

    dump_image(info_ptr, row_pointers, fpout);

    dump_tIME(info_ptr, fpout);

    dump_text(info_ptr, fpout);

    /* FIXME: dump gIFg, gIFx, and private chunks */
}

int sngd(FILE *fp, char *name, FILE *fpout)
/* read and decompile an SNG image presented on stdin */
{
   png_structp png_ptr;
   png_infop info_ptr;
   png_uint_32 width, height;
   int bit_depth, color_type, interlace_type, row;
   png_bytepp row_pointers;

   current_file = name;

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
      return(FAIL);
   }

   /* Allocate/initialize the memory for image information.  REQUIRED. */
   info_ptr = png_create_info_struct(png_ptr);
   if (info_ptr == NULL)
   {
      fclose(fp);
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
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
      return(FAIL);
   }

   /* Set up the input control if you are using standard C streams */
   png_init_io(png_ptr, fp);

   /* If we have already read some of the signature */
   /* png_set_sig_bytes(png_ptr, sig_read); */

   /* The call to png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk).  REQUIRED
    */
   png_read_info(png_ptr, info_ptr);

   png_get_IHDR(png_ptr, info_ptr, 
		&width, &height, &bit_depth, &color_type,
		&interlace_type, NULL, NULL);

   row_pointers = (png_bytepp)malloc(height * sizeof(png_bytep));
   for (row = 0; row < height; row++)
      row_pointers[row] = malloc(png_get_rowbytes(png_ptr, info_ptr));

   /* Now it's time to read the image.  One of these methods is REQUIRED */
   png_read_image(png_ptr, row_pointers);

   /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
   png_read_end(png_ptr, info_ptr);

   /* dump the image */
   sngdump(info_ptr, row_pointers, fpout);

   /* clean up after the read, and free any memory allocated - REQUIRED */
   png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

   /* close the file */
   fclose(fp);

   /* that's it */
   return(SUCCEED);
}

