/*
 * "$Id$"
 *
 *   PWG PPD mapping API implementation for CUPS.
 *
 *   Copyright 2010 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Contents:
 *
 *   _pwgCreateWithPPD()      - Create PWG mapping data from a PPD file.
 *   _pwgGetBin()             - Get the PWG output-bin keyword associated with a
 *                              PPD OutputBin.
 *   _pwgGetInputSlot()       - Get the PPD InputSlot associated with the job
 *                              attributes or a keyword string.
 *   _pwgGetMediaType()       - Get the PPD MediaType associated with the job
 *                              attributes or a keyword string.
 *   _pwgGetOutputBin()       - Get the PPD OutputBin associated with the
 *                              keyword string.
 *   _pwgGetPageSize()        - Get the PPD PageSize associated with the job
 *                              attributes or a keyword string.
 *   _pwgGetSize()            - Get the PWG size associated with a PPD PageSize.
 *   _pwgGetSource()          - Get the PWG media-source associated with a PPD
 *                              InputSlot.
 *   _pwgGetType()            - Get the PWG media-type associated with a PPD
 *                              MediaType.
 *   _pwgInputSlotForSource() - Get the InputSlot name for the given PWG source.
 *   _pwgMediaTypeForType()   - Get the MediaType name for the given PWG type.
 *   _pwgPageSizeForMedia()   - Get the PageSize name for the given media.
 *   pwg_ppdize_name()        - Convert an IPP keyword to a PPD keyword.
 *   pwg_unppdize_name()      - Convert a PPD keyword to a lowercase IPP
 *                              keyword.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include <math.h>


/*
 * Local functions...
 */

static void	pwg_ppdize_name(const char *ipp, char *name, size_t namesize);
static void	pwg_unppdize_name(const char *ppd, char *name, size_t namesize);


/*
 * '_pwgCreateWithPPD()' - Create PWG mapping data from a PPD file.
 */

_pwg_t *				/* O - PWG mapping data */
_pwgCreateWithPPD(ppd_file_t *ppd)	/* I - PPD file */
{
  int		i, j;			/* Looping vars */
  _pwg_t	*pwg;			/* PWG mapping data */
  ppd_option_t	*input_slot,		/* InputSlot option */
		*media_type,		/* MediaType option */
		*output_bin;		/* OutputBin option */
  ppd_choice_t	*choice;		/* Current InputSlot/MediaType */
  _pwg_map_t	*map;			/* Current source/type map */
  ppd_size_t	*ppd_size;		/* Current PPD size */
  _pwg_size_t	*pwg_size;		/* Current PWG size */
  char		pwg_keyword[3 + PPD_MAX_NAME + 1 + 12 + 1 + 12 + 3],
					/* PWG keyword string */
		ppd_name[PPD_MAX_NAME];	/* Normalized PPD name */
  const char	*pwg_name;		/* Standard PWG media name */
  _pwg_media_t	*pwg_media;		/* PWG media data */


  DEBUG_printf(("_pwgCreateWithPPD(ppd=%p)", ppd));

 /*
  * Range check input...
  */

  if (!ppd)
    return (NULL);

 /*
  * Allocate memory...
  */

  if ((pwg = calloc(1, sizeof(_pwg_t))) == NULL)
  {
    DEBUG_puts("_pwgCreateWithPPD: Unable to allocate _pwg_t.");
    goto create_error;
  }

 /*
  * Copy and convert size data...
  */

  if (ppd->num_sizes == 0)
  {
    DEBUG_puts("_pwgCreateWithPPD: No page sizes in PPD.");
    goto create_error;
  }

  if ((pwg->sizes = calloc(ppd->num_sizes, sizeof(_pwg_size_t))) == NULL)
  {
    DEBUG_printf(("_pwgCreateWithPPD: Unable to allocate %d _pwg_size_t's.",
                  ppd->num_sizes));
    goto create_error;
  }

  for (i = ppd->num_sizes, pwg_size = pwg->sizes, ppd_size = ppd->sizes;
       i > 0;
       i --, ppd_size ++)
  {
   /*
    * Don't copy over custom size...
    */

    if (!strcasecmp(ppd_size->name, "Custom"))
      continue;

   /*
    * Convert the PPD size name to the corresponding PWG keyword name.
    */

    if ((pwg_media = _pwgMediaForPPD(ppd_size->name)) != NULL)
    {
     /*
      * Standard name, do we have conflicts?
      */

      for (j = 0; j < pwg->num_sizes; j ++)
        if (!strcmp(pwg->sizes[j].map.pwg, pwg_media->pwg))
	{
	  pwg_media = NULL;
	  break;
	}
    }

    if (pwg_media)
    {
     /*
      * Standard name and no conflicts, use it!
      */

      pwg_name = pwg_media->pwg;
    }
    else
    {
     /*
      * Not a standard name; convert it to a PWG vendor name of the form:
      *
      *     pp_lowerppd_WIDTHxHEIGHTuu
      */

      pwg_name = pwg_keyword;

      pwg_unppdize_name(ppd_size->name, ppd_name, sizeof(ppd_name));
      _pwgGenerateSize(pwg_keyword, sizeof(pwg_keyword), NULL, ppd_name,
		       _PWG_FROMPTS(ppd_size->width),
		       _PWG_FROMPTS(ppd_size->length));
    }

   /*
    * Save this size...
    */

    pwg_size->map.ppd = _cupsStrAlloc(ppd_size->name);
    pwg_size->map.pwg = _cupsStrAlloc(pwg_name);
    pwg_size->width   = _PWG_FROMPTS(ppd_size->width);
    pwg_size->length  = _PWG_FROMPTS(ppd_size->length);
    pwg_size->left    = _PWG_FROMPTS(ppd_size->left);
    pwg_size->bottom  = _PWG_FROMPTS(ppd_size->bottom);
    pwg_size->right   = _PWG_FROMPTS(ppd_size->width - ppd_size->right);
    pwg_size->top     = _PWG_FROMPTS(ppd_size->length - ppd_size->top);

    pwg->num_sizes ++;
    pwg_size ++;
  }

  if (ppd->variable_sizes)
  {
   /*
    * Generate custom size data...
    */

    _pwgGenerateSize(pwg_keyword, sizeof(pwg_keyword), "custom", "max",
		     _PWG_FROMPTS(ppd->custom_max[0]),
		     _PWG_FROMPTS(ppd->custom_max[1]));
    pwg->custom_max_keyword = _cupsStrAlloc(pwg_keyword);
    pwg->custom_max_width   = _PWG_FROMPTS(ppd->custom_max[0]);
    pwg->custom_max_length  = _PWG_FROMPTS(ppd->custom_max[1]);

    _pwgGenerateSize(pwg_keyword, sizeof(pwg_keyword), "custom", "min",
		     _PWG_FROMPTS(ppd->custom_min[0]),
		     _PWG_FROMPTS(ppd->custom_min[1]));
    pwg->custom_min_keyword = _cupsStrAlloc(pwg_keyword);
    pwg->custom_min_width   = _PWG_FROMPTS(ppd->custom_min[0]);
    pwg->custom_min_length  = _PWG_FROMPTS(ppd->custom_min[1]);

    pwg->custom_size.left   = _PWG_FROMPTS(ppd->custom_margins[0]);
    pwg->custom_size.bottom = _PWG_FROMPTS(ppd->custom_margins[1]);
    pwg->custom_size.right  = _PWG_FROMPTS(ppd->custom_margins[2]);
    pwg->custom_size.top    = _PWG_FROMPTS(ppd->custom_margins[3]);
  }

 /*
  * Copy and convert InputSlot data...
  */

  if ((input_slot = ppdFindOption(ppd, "InputSlot")) != NULL)
  {
    if ((pwg->sources = calloc(input_slot->num_choices,
                               sizeof(_pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_pwgCreateWithPPD: Unable to allocate %d _pwg_map_t's "
                    "for InputSlot.", input_slot->num_choices));
      goto create_error;
    }

    pwg->num_sources = input_slot->num_choices;

    for (i = input_slot->num_choices, choice = input_slot->choices,
             map = pwg->sources;
	 i > 0;
	 i --, choice ++, map ++)
    {
      if (!strncasecmp(choice->choice, "Auto", 4) ||
          !strcasecmp(choice->choice, "Default"))
        pwg_name = "auto";
      else if (!strcasecmp(choice->choice, "Cassette"))
        pwg_name = "main";
      else if (!strncasecmp(choice->choice, "Multipurpose", 12) ||
               !strcasecmp(choice->choice, "MP") ||
               !strcasecmp(choice->choice, "MPTray"))
        pwg_name = "alternate";
      else if (!strcasecmp(choice->choice, "LargeCapacity"))
        pwg_name = "large-capacity";
      else if (!strncasecmp(choice->choice, "Lower", 5))
        pwg_name = "bottom";
      else if (!strncasecmp(choice->choice, "Middle", 6))
        pwg_name = "middle";
      else if (!strncasecmp(choice->choice, "Upper", 5))
        pwg_name = "top";
      else if (!strncasecmp(choice->choice, "Side", 4))
        pwg_name = "side";
      else if (!strcasecmp(choice->choice, "Roll") ||
               !strcasecmp(choice->choice, "Roll1"))
        pwg_name = "main-roll";
      else if (!strcasecmp(choice->choice, "Roll2"))
        pwg_name = "alternate-roll";
      else
      {
       /*
        * Convert PPD name to lowercase...
	*/

        pwg_name = pwg_keyword;
	pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword));
      }

      map->pwg = _cupsStrAlloc(pwg_name);
      map->ppd = _cupsStrAlloc(choice->choice);
    }
  }

 /*
  * Copy and convert MediaType data...
  */

  if ((media_type = ppdFindOption(ppd, "MediaType")) != NULL)
  {
    if ((pwg->types = calloc(media_type->num_choices,
                             sizeof(_pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_pwgCreateWithPPD: Unable to allocate %d _pwg_map_t's "
                    "for MediaType.", media_type->num_choices));
      goto create_error;
    }

    pwg->num_types = media_type->num_choices;

    for (i = media_type->num_choices, choice = media_type->choices,
             map = pwg->types;
	 i > 0;
	 i --, choice ++, map ++)
    {
      if (!strncasecmp(choice->choice, "Auto", 4) ||
          !strcasecmp(choice->choice, "Any") ||
          !strcasecmp(choice->choice, "Default"))
        pwg_name = "auto";
      else if (!strncasecmp(choice->choice, "Card", 4))
        pwg_name = "cardstock";
      else if (!strncasecmp(choice->choice, "Env", 3))
        pwg_name = "envelope";
      else if (!strncasecmp(choice->choice, "Gloss", 5))
        pwg_name = "photographic-glossy";
      else if (!strcasecmp(choice->choice, "HighGloss"))
        pwg_name = "photographic-high-gloss";
      else if (!strcasecmp(choice->choice, "Matte"))
        pwg_name = "photographic-matte";
      else if (!strncasecmp(choice->choice, "Plain", 5))
        pwg_name = "stationery";
      else if (!strncasecmp(choice->choice, "Coated", 6))
        pwg_name = "stationery-coated";
      else if (!strcasecmp(choice->choice, "Inkjet"))
        pwg_name = "stationery-inkjet";
      else if (!strcasecmp(choice->choice, "Letterhead"))
        pwg_name = "stationery-letterhead";
      else if (!strncasecmp(choice->choice, "Preprint", 8))
        pwg_name = "stationery-preprinted";
      else if (!strncasecmp(choice->choice, "Transparen", 10))
        pwg_name = "transparency";
      else
      {
       /*
        * Convert PPD name to lowercase...
	*/

        pwg_name = pwg_keyword;
	pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword));
      }

      map->pwg = _cupsStrAlloc(pwg_name);
      map->ppd = _cupsStrAlloc(choice->choice);
    }
  }


 /*
  * Copy and convert OutputBin data...
  */

  if ((output_bin = ppdFindOption(ppd, "OutputBin")) != NULL)
  {
    if ((pwg->types = calloc(output_bin->num_choices,
                             sizeof(_pwg_map_t))) == NULL)
    {
      DEBUG_printf(("_pwgCreateWithPPD: Unable to allocate %d _pwg_map_t's "
                    "for OutputBin.", output_bin->num_choices));
      goto create_error;
    }

    pwg->num_bins = output_bin->num_choices;

    for (i = output_bin->num_choices, choice = output_bin->choices,
             map = pwg->types;
	 i > 0;
	 i --, choice ++, map ++)
    {
      pwg_unppdize_name(choice->choice, pwg_keyword, sizeof(pwg_keyword));

      map->pwg = _cupsStrAlloc(pwg_keyword);
      map->ppd = _cupsStrAlloc(choice->choice);
    }
  }

  return (pwg);

 /*
  * If we get here we need to destroy the PWG mapping data and return NULL...
  */

  create_error:

  _cupsSetError(IPP_INTERNAL_ERROR, _("Out of memory."), 1);
  _pwgDestroy(pwg);

  return (NULL);
}


/*
 * '_pwgGetBin()' - Get the PWG output-bin keyword associated with a PPD
 *                  OutputBin.
 */

const char *				/* O - output-bin or NULL */
_pwgGetBin(_pwg_t     *pwg,		/* I - PWG mapping data */
	   const char *output_bin)	/* I - PPD OutputBin string */
{
  int	i;				/* Looping var */


 /*
  * Range check input...
  */

  if (!pwg || !output_bin)
    return (NULL);

 /*
  * Look up the OutputBin string...
  */


  for (i = 0; i < pwg->num_bins; i ++)
    if (!strcasecmp(output_bin, pwg->bins[i].ppd))
      return (pwg->bins[i].pwg);

  return (NULL);
}


/*
 * '_pwgGetInputSlot()' - Get the PPD InputSlot associated with the job
 *                        attributes or a keyword string.
 */

const char *				/* O - PPD InputSlot or NULL */
_pwgGetInputSlot(_pwg_t     *pwg,	/* I - PWG mapping data */
                 ipp_t      *job,	/* I - Job attributes or NULL */
		 const char *keyword)	/* I - Keyword string or NULL */
{
 /*
  * Range check input...
  */

  if (!pwg || pwg->num_sources == 0 || (!job && !keyword))
    return (NULL);

  if (job && !keyword)
  {
   /*
    * Lookup the media-col attribute and any media-source found there...
    */

    ipp_attribute_t	*media_col,	/* media-col attribute */
			*media_source;	/* media-source attribute */

    media_col = ippFindAttribute(job, "media-col", IPP_TAG_BEGIN_COLLECTION);
    if (media_col &&
        (media_source = ippFindAttribute(media_col->values[0].collection,
                                         "media-source",
	                                 IPP_TAG_KEYWORD)) != NULL)
      keyword = media_source->values[0].string.text;
  }

  if (keyword)
  {
    int	i;				/* Looping var */

    for (i = 0; i < pwg->num_sources; i ++)
      if (!strcasecmp(keyword, pwg->sources[i].pwg))
        return (pwg->sources[i].ppd);
  }

  return (NULL);
}


/*
 * '_pwgGetMediaType()' - Get the PPD MediaType associated with the job
 *                        attributes or a keyword string.
 */

const char *				/* O - PPD MediaType or NULL */
_pwgGetMediaType(_pwg_t     *pwg,	/* I - PWG mapping data */
                 ipp_t      *job,	/* I - Job attributes or NULL */
		 const char *keyword)	/* I - Keyword string or NULL */
{
 /*
  * Range check input...
  */

  if (!pwg || pwg->num_types == 0 || (!job && !keyword))
    return (NULL);

  if (job && !keyword)
  {
   /*
    * Lookup the media-col attribute and any media-source found there...
    */

    ipp_attribute_t	*media_col,	/* media-col attribute */
			*media_type;	/* media-type attribute */

    media_col = ippFindAttribute(job, "media-col", IPP_TAG_BEGIN_COLLECTION);
    if (media_col)
    {
      if ((media_type = ippFindAttribute(media_col->values[0].collection,
                                         "media-type",
	                                 IPP_TAG_KEYWORD)) == NULL)
	media_type = ippFindAttribute(media_col->values[0].collection,
				      "media-type", IPP_TAG_NAME);

      if (media_type)
	keyword = media_type->values[0].string.text;
    }
  }

  if (keyword)
  {
    int	i;				/* Looping var */

    for (i = 0; i < pwg->num_types; i ++)
      if (!strcasecmp(keyword, pwg->types[i].pwg))
        return (pwg->types[i].ppd);
  }

  return (NULL);
}


/*
 * '_pwgGetOutputBin()' - Get the PPD OutputBin associated with the keyword
 *                        string.
 */

const char *				/* O - PPD OutputBin or NULL */
_pwgGetOutputBin(_pwg_t     *pwg,	/* I - PWG mapping data */
		 const char *output_bin)/* I - Keyword string */
{
  int	i;				/* Looping var */


 /*
  * Range check input...
  */

  if (!pwg || !output_bin)
    return (NULL);

 /*
  * Look up the OutputBin string...
  */


  for (i = 0; i < pwg->num_bins; i ++)
    if (!strcasecmp(output_bin, pwg->bins[i].pwg))
      return (pwg->bins[i].ppd);

  return (NULL);
}


/*
 * '_pwgGetPageSize()' - Get the PPD PageSize associated with the job
 *                       attributes or a keyword string.
 */

const char *				/* O - PPD PageSize or NULL */
_pwgGetPageSize(_pwg_t     *pwg,	/* I - PWG mapping data */
                ipp_t      *job,	/* I - Job attributes or NULL */
		const char *keyword,	/* I - Keyword string or NULL */
		int        *exact)	/* I - 1 if exact match, 0 otherwise */
{
  int		i;			/* Looping var */
  _pwg_size_t	*size,			/* Current size */
		*closest,		/* Closest size */
		jobsize;		/* Size data from job */
  int		margins_set,		/* Were the margins set? */
		dwidth,			/* Difference in width */
		dlength,		/* Difference in length */
		dleft,			/* Difference in left margins */
		dright,			/* Difference in right margins */
		dbottom,		/* Difference in bottom margins */
		dtop,			/* Difference in top margins */
		dmin,			/* Minimum difference */
		dclosest;		/* Closest difference */


 /*
  * Range check input...
  */

  if (!pwg || (!job && !keyword))
    return (NULL);

  if (exact)
    *exact = 0;

  if (job && !keyword)
  {
   /*
    * Get the size using media-col or media, with the preference being
    * media-col.
    */

    if (!_pwgInitSize(&jobsize, job, &margins_set))
      return (NULL);
  }
  else
  {
   /*
    * Get the size using a media keyword...
    */

    _pwg_media_t	*media;		/* Media definition */


    if ((media = _pwgMediaForPWG(keyword)) == NULL)
      if ((media = _pwgMediaForLegacy(keyword)) == NULL)
        return (NULL);

    jobsize.width  = media->width;
    jobsize.length = media->length;
    margins_set    = 0;
  }

 /*
  * Now that we have the dimensions and possibly the margins, look at the
  * available sizes and find the match...
  */

  closest  = NULL;
  dclosest = 999999999;

  for (i = pwg->num_sizes, size = pwg->sizes; i > 0; i --, size ++)
  {
   /*
    * Adobe uses a size matching algorithm with an epsilon of 5 points, which
    * is just about 176/2540ths...
    */

    dwidth  = size->width - jobsize.width;
    dlength = size->length - jobsize.length;

    if (dwidth <= -176 || dwidth >= 176 || dlength <= -176 || dlength >= 176)
      continue;

    if (margins_set)
    {
     /*
      * Use a tighter epsilon of 1 point (35/2540ths) for margins...
      */

      dleft   = size->left - jobsize.left;
      dright  = size->right - jobsize.right;
      dtop    = size->top - jobsize.top;
      dbottom = size->bottom - jobsize.bottom;

      if (dleft <= -35 || dleft >= 35 || dright <= -35 || dright >= 35 ||
          dtop <= -35 || dtop >= 35 || dbottom <= -35 || dbottom >= 35)
      {
        dleft   = dleft < 0 ? -dleft : dleft;
        dright  = dright < 0 ? -dright : dright;
        dbottom = dbottom < 0 ? -dbottom : dbottom;
        dtop    = dtop < 0 ? -dtop : dtop;
	dmin    = dleft + dright + dbottom + dtop;

        if (dmin < dclosest)
	{
	  dclosest = dmin;
	  closest  = size;
	}

	continue;
      }
    }

    if (exact)
      *exact = 1;

    return (size->map.ppd);
  }

  if (closest)
    return (closest->map.ppd);

 /*
  * If we get here we need to check for custom page size support...
  */

  if (jobsize.width >= pwg->custom_min_width &&
      jobsize.width <= pwg->custom_max_width &&
      jobsize.length >= pwg->custom_min_length &&
      jobsize.length <= pwg->custom_max_length)
  {
   /*
    * In range, format as Custom.WWWWxLLLL (points).
    */

    snprintf(pwg->custom_ppd_size, sizeof(pwg->custom_ppd_size), "Custom.%dx%d",
             (int)_PWG_TOPTS(jobsize.width), (int)_PWG_TOPTS(jobsize.length));

    if (margins_set && exact)
    {
      dleft   = pwg->custom_size.left - jobsize.left;
      dright  = pwg->custom_size.right - jobsize.right;
      dtop    = pwg->custom_size.top - jobsize.top;
      dbottom = pwg->custom_size.bottom - jobsize.bottom;

      if (dleft > -35 && dleft < 35 && dright > -35 && dright < 35 &&
          dtop > -35 && dtop < 35 && dbottom > -35 && dbottom < 35)
	*exact = 1;
    }
    else if (exact)
      *exact = 1;

    return (pwg->custom_ppd_size);
  }

 /*
  * No custom page size support or the size is out of range - return NULL.
  */

  return (NULL);
}


/*
 * '_pwgGetSize()' - Get the PWG size associated with a PPD PageSize.
 */

_pwg_size_t *				/* O - PWG size or NULL */
_pwgGetSize(_pwg_t     *pwg,		/* I - PWG mapping data */
            const char *page_size)	/* I - PPD PageSize */
{
  int		i;
  _pwg_size_t	*size;			/* Current size */


  if (!strncasecmp(page_size, "Custom.", 7))
  {
   /*
    * Custom size; size name can be one of the following:
    *
    *    Custom.WIDTHxLENGTHin    - Size in inches
    *    Custom.WIDTHxLENGTHft    - Size in feet
    *    Custom.WIDTHxLENGTHcm    - Size in centimeters
    *    Custom.WIDTHxLENGTHmm    - Size in millimeters
    *    Custom.WIDTHxLENGTHm     - Size in meters
    *    Custom.WIDTHxLENGTH[pt]  - Size in points
    */

    double		w, l;		/* Width and length of page */
    char		*ptr;		/* Pointer into PageSize */
    struct lconv	*loc;		/* Locale data */

    loc = localeconv();
    w   = (float)_cupsStrScand(page_size + 7, &ptr, loc);
    if (!ptr || *ptr != 'x')
      return (NULL);

    l = (float)_cupsStrScand(ptr + 1, &ptr, loc);
    if (!ptr)
      return (NULL);

    if (!strcasecmp(ptr, "in"))
    {
      w *= 2540.0;
      l *= 2540.0;
    }
    else if (!strcasecmp(ptr, "ft"))
    {
      w *= 12.0 * 2540.0;
      l *= 12.0 * 2540.0;
    }
    else if (!strcasecmp(ptr, "mm"))
    {
      w *= 100.0;
      l *= 100.0;
    }
    else if (!strcasecmp(ptr, "cm"))
    {
      w *= 1000.0;
      l *= 1000.0;
    }
    else if (!strcasecmp(ptr, "m"))
    {
      w *= 100000.0;
      l *= 100000.0;
    }
    else
    {
      w *= 2540.0 / 72.0;
      l *= 2540.0 / 72.0;
    }

    pwg->custom_size.width  = (int)w;
    pwg->custom_size.length = (int)l;

    return (&(pwg->custom_size));
  }

 /*
  * Not a custom size - look it up...
  */

  for (i = pwg->num_sizes, size = pwg->sizes; i > 0; i --, size ++)
    if (!strcasecmp(page_size, size->map.ppd))
      return (size);

  return (NULL);
}


/*
 * '_pwgGetSource()' - Get the PWG media-source associated with a PPD InputSlot.
 */

const char *				/* O - PWG media-source keyword */
_pwgGetSource(_pwg_t     *pwg,		/* I - PWG mapping data */
              const char *input_slot)	/* I - PPD InputSlot */
{
  int		i;			/* Looping var */
  _pwg_map_t	*source;		/* Current source */


  for (i = pwg->num_sources, source = pwg->sources; i > 0; i --, source ++)
    if (!strcasecmp(input_slot, source->ppd))
      return (source->pwg);

  return (NULL);
}


/*
 * '_pwgGetType()' - Get the PWG media-type associated with a PPD MediaType.
 */

const char *				/* O - PWG media-type keyword */
_pwgGetType(_pwg_t     *pwg,		/* I - PWG mapping data */
	    const char *media_type)	/* I - PPD MediaType */
{
  int		i;			/* Looping var */
  _pwg_map_t	*type;			/* Current type */


  for (i = pwg->num_types, type = pwg->types; i > 0; i --, type ++)
    if (!strcasecmp(media_type, type->ppd))
      return (type->pwg);

  return (NULL);
}


/*
 * '_pwgInputSlotForSource()' - Get the InputSlot name for the given PWG source.
 */

const char *				/* O - InputSlot name */
_pwgInputSlotForSource(
    const char *media_source,		/* I - PWG media-source */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
  if (strcasecmp(media_source, "main"))
    strlcpy(name, "Cassette", namesize);
  else if (strcasecmp(media_source, "alternate"))
    strlcpy(name, "Multipurpose", namesize);
  else if (strcasecmp(media_source, "large-capacity"))
    strlcpy(name, "LargeCapacity", namesize);
  else if (strcasecmp(media_source, "bottom"))
    strlcpy(name, "Lower", namesize);
  else if (strcasecmp(media_source, "middle"))
    strlcpy(name, "Middle", namesize);
  else if (strcasecmp(media_source, "top"))
    strlcpy(name, "Upper", namesize);
  else if (strcasecmp(media_source, "rear"))
    strlcpy(name, "Rear", namesize);
  else if (strcasecmp(media_source, "side"))
    strlcpy(name, "Side", namesize);
  else if (strcasecmp(media_source, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (strcasecmp(media_source, "main-roll"))
    strlcpy(name, "Roll", namesize);
  else if (strcasecmp(media_source, "alternate-roll"))
    strlcpy(name, "Roll2", namesize);
  else
    pwg_ppdize_name(media_source, name, namesize);

  return (name);
}


/*
 * '_pwgMediaTypeForType()' - Get the MediaType name for the given PWG type.
 */

const char *				/* O - MediaType name */
_pwgMediaTypeForType(
    const char *media_type,		/* I - PWG media-source */
    char       *name,			/* I - Name buffer */
    size_t     namesize)		/* I - Size of name buffer */
{
  if (strcasecmp(media_type, "auto"))
    strlcpy(name, "Auto", namesize);
  else if (strcasecmp(media_type, "cardstock"))
    strlcpy(name, "Cardstock", namesize);
  else if (strcasecmp(media_type, "envelope"))
    strlcpy(name, "Envelope", namesize);
  else if (strcasecmp(media_type, "photographic-glossy"))
    strlcpy(name, "Glossy", namesize);
  else if (strcasecmp(media_type, "photographic-high-gloss"))
    strlcpy(name, "HighGloss", namesize);
  else if (strcasecmp(media_type, "photographic-matte"))
    strlcpy(name, "Matte", namesize);
  else if (strcasecmp(media_type, "stationery"))
    strlcpy(name, "Plain", namesize);
  else if (strcasecmp(media_type, "stationery-coated"))
    strlcpy(name, "Coated", namesize);
  else if (strcasecmp(media_type, "stationery-inkjet"))
    strlcpy(name, "Inkjet", namesize);
  else if (strcasecmp(media_type, "stationery-letterhead"))
    strlcpy(name, "Letterhead", namesize);
  else if (strcasecmp(media_type, "stationery-preprinted"))
    strlcpy(name, "Preprinted", namesize);
  else if (strcasecmp(media_type, "transparency"))
    strlcpy(name, "Transparency", namesize);
  else
    pwg_ppdize_name(media_type, name, namesize);

  return (name);
}


/*
 * '_pwgPageSizeForMedia()' - Get the PageSize name for the given media.
 */

const char *				/* O - PageSize name */
_pwgPageSizeForMedia(
    _pwg_media_t *media,		/* I - Media */
    char         *name,			/* I - PageSize name buffer */
    size_t       namesize)		/* I - Size of name buffer */
{
  const char	*sizeptr,		/* Pointer to size in PWG name */
		*dimptr;		/* Pointer to dimensions in PWG name */


 /*
  * Range check input...
  */

  if (!media || !name || namesize < PPD_MAX_NAME)
    return (NULL);

 /*
  * Copy or generate a PageSize name...
  */

  if (media->ppd)
  {
   /*
    * Use a standard Adobe name...
    */

    strlcpy(name, media->ppd, namesize);
  }
  else if (!media->pwg || !strncmp(media->pwg, "custom_", 7) ||
           (sizeptr = strchr(media->pwg, '_')) == NULL ||
	   (dimptr = strchr(sizeptr + 1, '_')) == NULL ||
	   (dimptr - sizeptr) > namesize)
  {
   /*
    * Use a name of the form "wNNNhNNN"...
    */

    snprintf(name, namesize, "w%dh%d", (int)_PWG_TOPTS(media->width),
             (int)_PWG_TOPTS(media->length));
  }
  else
  {
   /*
    * Copy the size name from class_sizename_dimensions...
    */

    memcpy(name, sizeptr + 1, dimptr - sizeptr - 1);
    name[dimptr - sizeptr - 1] = '\0';
  }

  return (name);
}


/*
 * 'pwg_ppdize_name()' - Convert an IPP keyword to a PPD keyword.
 */

static void
pwg_ppdize_name(const char *ipp,	/* I - IPP keyword */
                char       *name,	/* I - Name buffer */
		size_t     namesize)	/* I - Size of name buffer */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */


  *name = toupper(*ipp++);

  for (ptr = name + 1, end = name + namesize - 1; *ipp && ptr < end;)
  {
    if (*ipp == '-' && isalpha(ipp[1] & 255))
    {
      ipp ++;
      *ptr++ = toupper(*ipp++ & 255);
    }
    else
      *ptr++ = *ipp++;
  }

  *ptr = '\0';
}


/*
 * 'pwg_unppdize_name()' - Convert a PPD keyword to a lowercase IPP keyword.
 */

static void
pwg_unppdize_name(const char *ppd,	/* I - PPD keyword */
		  char       *name,	/* I - Name buffer */
                  size_t     namesize)	/* I - Size of name buffer */
{
  char	*ptr,				/* Pointer into name buffer */
	*end;				/* End of name buffer */


  for (ptr = name, end = name + namesize - 1; *ppd && ptr < end; ppd ++)
  {
    if (isalnum(*ppd & 255) || *ppd == '-' || *ppd == '.')
      *ptr++ = tolower(*ppd & 255);
    else if (*ppd == '_')
      *ptr++ = '-';

    if (!isupper(*ppd & 255) && isalnum(*ppd & 255) &&
	isupper(ppd[1] & 255) && ptr < end)
      *ptr++ = '-';
  }

  *ptr = '\0';
}


/*
 * End of "$Id$".
 */
