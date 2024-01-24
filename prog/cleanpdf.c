/*====================================================================*
 -  Copyright (C) 2001 Leptonica.  All rights reserved.
 -
 -  Redistribution and use in source and binary forms, with or without
 -  modification, are permitted provided that the following conditions
 -  are met:
 -  1. Redistributions of source code must retain the above copyright
 -     notice, this list of conditions and the following disclaimer.
 -  2. Redistributions in binary form must reproduce the above
 -     copyright notice, this list of conditions and the following
 -     disclaimer in the documentation and/or other materials
 -     provided with the distribution.
 -
 -  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 -  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 -  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 -  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANY
 -  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 -  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 -  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 -  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 -  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 -  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 -  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *====================================================================*/

/*
 * cleanpdf.c
 *
 *    This program is intended to take as input a set of pdf files that have
 *    been constructed from poorly compressed images -- typically images
 *    that have been scanned in grayscale or color but should be rendered
 *    in black and white (1 bpp).  It cleans, compresses and concatenates
 *    them, generating a single pdf composed of tiff-g4 compressed images.
 *
 *    It will also take as input clean, orthographically-generated pdfs,
 *    and concatenate them into a single pdf file of images.
 *
 *    Syntax:
 *       cleanpdf basedir resolution contrast rotation opensize title fileout
 *
 *    A typical command is:
 *        cleanpdf . 300 0 0 0 none <name-of-output-pdf-file>
 *
 *    The %basedir is a directory where the input pdf files are located.
 *    The program will operate on every file in this directory with
 *    the ".pdf" extension.  Use "." if the files are in the current directory.
 *
 *    We use adaptive thresholding.  After background normalization, the
 *    a global threshold of about 180 is effective, and the resulting
 *    binarization is relatively insensitive to the value.
 *
 *    The output %resolution parameter can take on two values:
 *       300  (binarize at the same resolution as the gray or color input,
 *             which is typically 300 ppi)
 *       600  (binarize at twice the resolution of the gray or color input,
 *             by doing an interpolated 2x expansion on the grayscale
 *             image, followed by thresholding to 1 bpp)
 *    At 300 ppi, an 8.5 x 11 page would have 2550 x 3300 pixels.
 *    You can also input 0 for the default output resolution of 300 ppi.
 *
 *    The %contrast parameter adjusts the binarization to avoid losing input
 *    details that are too light.  It takes on 10 values from 1 to 10, where
 *    1 is the lightest value and it removes noise.  Use of 0 will default
 *    to 1.  Suggested value is 1 unless important details are lost on
 *    binarization.
 *
 *    The %rotation parameter is an integer that specifies the rotation
 *    to be applied to each image:
 *       0      no rotation   (default)
 *       1      90 degrees cw
 *       2      180 degrees cw
 *       3      270 degrees cw
 *
 *    The %opensize parameter is the size of a square morphological
 *    structuring element used to remove speckle noise generated by poor
 *    quality scanner thresholding to 1 bpp on images with dark background.
 *    Allowed values are 0 (no-op), 2 and 3, where 2 causes a 2x2 opening, etc.
 *    Nonzero values are rarely used.  2 is recommended because 3, which
 *    removes any FG pixels that are not part of a 3x3 block of FG pixels,
 *    is often too aggressive.
 *
 *    The %title is the title given to the pdf.  Use %title == "none"
 *    to omit the title.
 *
 *    The pdf output is written to %fileout.  It is advisable (but not
 *    required) to have a '.pdf' extension.
 *
 *    Whenever possible, the images will be deskewed.
 *
 *    As the first step in processing, images are saved in the ./image
 *    directory as RGB at 300 ppi in ppm format.  Each image is about 26MB.
 *    Delete those images after use.
 *
 *    Some pdf files have oversize media boxes.  PDF is a
 *    resolution-independent format for storing data that can be imaged.
 *    Usually the data is stored in fonts, which are a description of the
 *    shape that can be rendered at different image resolutions.  We deal
 *    here with images that are made up of a fixed number of pixels, and
 *    thus are not resolution independent.  It is necessary for image
 *    specification to include data for the renderer that says how big
 *    (in inches) to display or print the image.  That is done with /MediaBox,
 *    whose 3rd and 4th parameters are the width and height of the output
 *    image in printer points.  (1 printer point = 1/72 inch).  To prevent
 *    pdf files with incorrect use of /MediaBox from forcing the renderer
 *    to make oversize images, we find the median media box width and height.
 *    If the larger of the two is significantly bigger than 792 printer
 *    points, corresponding to 11 inches, we compensate with a resolution
 *    below 300 ppi that will make the largest image dimension about
 *    3300 pixels.  If no media box is found, it is necessary to render
 *    a test image using a small resolution and find the size of the image.
 *
 *    Notes on using filenames with internal spaces.
 *    * The file-handling functions in leptonica do not support filenames
 *      that have spaces.  To use cleanpdf in linux with such input
 *      filenames, substitute an ascii character for the spaces; e.g., '^'.
 *         char *newstr = stringReplaceEachSubstr(str, " ", "^", NULL);
 *      Then run cleanpdf on the file(s).
 *    * To get an output filename with spaces, use single quotes; e.g.,
 *         cleanpdf dir [...] title 'quoted filename with spaces'
 *
 *    N.B.  This requires the Poppler package of pdf utilities, such as
 *          pdfimages and pdftoppm.  For non-unix systems, this requires
 *          installation of the cygwin Poppler package:
 *       https://cygwin.com/cgi-bin2/package-cat.cgi?file=x86/poppler/
 *              poppler-0.26.5-1
 */

#ifdef HAVE_CONFIG_H
#include <config_auto.h>
#endif  /* HAVE_CONFIG_H */

#ifdef _WIN32
# if defined(_MSC_VER) || defined(__MINGW32__)
#  include <direct.h>
# else
#  include <io.h>
# endif  /* _MSC_VER || __MINGW32__ */
#endif  /* _WIN32 */

    /* Set to 1 to use pdftoppm (recommended); 0 for pdfimages */
#define   USE_PDFTOPPM     1

#include "string.h"
#include <sys/stat.h>
#include <sys/types.h>
#include "allheaders.h"

l_int32 main(int    argc,
             char **argv)
{
char     buf[256];
char    *basedir, *fname, *tail, *basename, *imagedir, *firstfile, *title;
char    *fileout;
l_int32  i, n, res, contrast, rotation, opensize, render_res, ret;
SARRAY  *sa;

    if (argc != 8)
        return ERROR_INT(
            "\n  Syntax: cleanpdf basedir resolution "
            "contrast rotation opensize title fileout",
            __func__, 1);
    basedir = argv[1];
    res = atoi(argv[2]);
    contrast = atoi(argv[3]);
    rotation = atoi(argv[4]);
    opensize = atoi(argv[5]);
    title = argv[6];
    fileout = argv[7];
    if (res == 0)
        res = 300;
    if (res != 300 && res != 600) {
        L_ERROR("invalid res = %d; res must be in {0, 300, 600}\n",
                __func__, res);
        return 1;
    }
    if (contrast == 0) contrast = 1;
    if (contrast < 1 || contrast > 10) {
        L_ERROR("invalid contrast = %d; contrast must be in {1,...,10}\n",
                __func__, contrast);
        return 1;
    }
    if (rotation < 0 || rotation > 3) {
        L_ERROR("invalid rotation = %d; rotation must be in  {0,1,2,3}\n",
                __func__, rotation);
        return 1;
    }
    if (opensize > 3) {
        L_ERROR("invalid opensize = %d; opensize must be <= 3\n",
                __func__, opensize);
        return 1;
    }
    setLeptDebugOK(1);

        /* Set up a directory for temp images */
    imagedir = stringJoin(basedir, "/image");
  #ifndef _WIN32
    mkdir(imagedir, 0777);
  #else
    _mkdir(imagedir);
  #endif  /* _WIN32 */

        /* Get the names of the input pdf files */
    if ((sa = getSortedPathnamesInDirectory(basedir, "pdf", 0, 0)) == NULL)
        return ERROR_INT("files not found", __func__, 1);
    sarrayWriteStderr(sa);
    n = sarrayGetCount(sa);

        /* Figure out the resolution to use with the image renderer.
           This first checks the media box sizes, which give the output
           image size in printer points (1/72 inch).  The largest expected
           output image has a max dimension of about 11 inches, corresponding
           to 792 points.  At a resolution of 300 ppi, the max image size
           is then 3300.  For robustness, use the median of media box sizes.
           If the max dimension of this median is significantly larger than
           792, reduce the input resolution to the renderer. Specifically:
            * Calculate the median of the MediaBox widths and heights.
            * If the max exceeds 850, reduce the resolution so that the max
              dimension of the rendered image is 3300.  The new resolution
              input to the renderer is reduced from 300 by the factor:
                            (792 / medmax)
           If the media boxes are not found, render a page using a small
           given resolution (72) and use the max dimension to find the
           resolution that will produce a 3300 pixel size output.  */
    firstfile = sarrayGetString(sa, 0, L_NOCOPY);
    getPdfRendererResolution(firstfile, imagedir, &render_res);

        /* Rasterize: use either
         *     pdftoppm -r res fname outroot  (-r res renders output at res ppi)
         * or
         *     pdfimages -j fname outroot   (-j outputs jpeg if input is dct)
         * Use of pdftoppm:
         *    This works on all pdf pages, both wrapped images and pages that
         *    were made orthographically.  The default output resolution for
         *    pdftoppm is 150 ppi, but we use 300 ppi.  This makes large
         *    uncompressed files (e.g., a standard size RGB page image at 300
         *    ppi is 25 MB), but it is very fast.  This is now preferred over
         *    using pdfimages.
         * Use of pdfimages:
         *    This only works when all pages are pdf wrappers around images.
         *    In some cases, it scrambles the order of the output pages
         *    and inserts extra images. */
    for (i = 0; i < n; i++) {
        fname = sarrayGetString(sa, i, L_NOCOPY);
        splitPathAtDirectory(fname, NULL, &tail);
        splitPathAtExtension(tail, &basename, NULL);
  #if USE_PDFTOPPM
        snprintf(buf, sizeof(buf), "pdftoppm -r %d %s %s/%s",
                 render_res, fname, imagedir, basename);
  #else
        snprintf(buf, sizeof(buf), "pdfimages -j %s %s/%s",
                 fname, imagedir, basename);
  #endif  /* USE_PDFTOPPM */
        lept_free(tail);
        lept_free(basename);
        lept_stderr("%s\n", buf);
        callSystemDebug(buf);   /* pdfimages or pdftoppm */
    }
    sarrayDestroy(&sa);

        /* Clean, deskew and compress */
    sa = getSortedPathnamesInDirectory(imagedir, NULL, 0, 0);
    lept_free(imagedir);
    sarrayWriteStderr(sa);
    lept_stderr("cleaning ...\n");
    cleanTo1bppFilesToPdf(sa, res, contrast, rotation, opensize,
                          title, fileout);
    sarrayDestroy(&sa);
    return 0;
}
