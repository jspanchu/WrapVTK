/*=========================================================================

  Program:   WrapVTK
  Module:    vtkWrapXML.c

  Copyright (c) 2010 David Gobbi
  All rights reserved.

  Copyright (c) 2008, Queen's University, Kingston, Ontario, Canada
  All rights reserved.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  Please see Copyright.txt for more details.

=========================================================================*/

/**
 The vtkWrapXML program will read VTK header files and produce an XML
 representation of the interface to the VTK classes.  The main entry
 point in this file is  vtkParseOutput(FILE *fp, FileInfo *data)
 where "FILE *fp" is the output file handle and "FileInfo *data" is
 the data structure that vtkParse.tab.c creates from the header.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "vtkParse.h"
#include "vtkParseProperties.h"
#include "vtkParseHierarchy.h"
#include "vtkParseMerge.h"
#include "vtkParseMain.h"
#include "vtkConfigure.h"

/* ----- XML state information ----- */

typedef struct _wrapxml_state
{
  FILE *file; /* the file being written to */
  int indentation; /* current indentation level */
  int unclosed; /* true if current tag is not closed */
} wrapxml_state_t;

/* ----- XML utility functions ----- */

/* The indentation string, default is two spaces */
#define VTKXML_INDENT "  "

/* indent to the specified indentation level */
static const char *indent(int indentation)
{
  static const char *indentString[6] = {
    "",
    VTKXML_INDENT,
    VTKXML_INDENT VTKXML_INDENT,
    VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT,
    VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT,
    VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT VTKXML_INDENT,
  };

  /* after 6 indentation levels, go back to zero */
  return indentString[indentation % 6];
}

/**
 * Convert special characters in a string into their escape codes,
 * so that the string can be quoted in an xml file (the specified
 * maxlen must be at least 32 chars)
 */
static const char *vtkWrapXML_Quote(const char *comment, size_t maxlen)
{
  static char *result = 0;
  static size_t oldmaxlen = 0;
  size_t i, j, n;

  if (maxlen > oldmaxlen)
    {
    if (result)
      {
      free(result);
      }
    result = (char *)malloc((size_t)(maxlen+1));
    oldmaxlen = maxlen;
    }

  if (comment == NULL)
    {
    return "";
    }

  j = 0;

  n = (int)strlen(comment);
  for (i = 0; i < n; i++)
    {
    if (comment[i] == '<')
      {
      strcpy(&result[j],"&lt;");
      j += 4;
      }
    else if (comment[i] == '>')
      {
      strcpy(&result[j],"&gt;");
      j += 4;
      }
    else if (comment[i] == '&')
      {
      strcpy(&result[j],"&amp;");
      j += 5;
      }
    else if (comment[i] == '\"')
      {
      strcpy(&result[j],"&quot;");
      j += 6;
      }
    else if (comment[i] == '\'')
      {
      strcpy(&result[j],"&apos;");
      j += 6;
      }
    else if (isprint(comment[i]))
      {
      result[j] = comment[i];
      j++;
      }
    else if (isspace(comment[i]))
      { /* also copy non-printing characters */
      result[j] = comment[i];
      j++;
      }

    /* add trailing ellipsis if too long */
    if (j >= maxlen - 5)
      {
      sprintf(&result[j]," ...");
      j += (int)strlen(" ...");
      break;
      }
    }
  result[j] = '\0';

  return result;
}

/**
 * Print multi-line text at the specified indentation level.
 */
static void vtkWrapXML_MultiLineText(wrapxml_state_t *w, const char *cp)
{
  size_t i, j;
  char temp[512];

  for (i = 0; cp && cp[i] != '\0'; i++)
    {
    for (j = 0; j < 200 && cp[i] != '\0' && cp[i] != '\n'; j++)
      {
      temp[j] = cp[i++];
      }

    while (j > 0 &&
           (temp[j-1] == ' ' || temp[j-1] == '\t' || temp[j-1] == '\r'))
      {
      j--;
      }

    temp[j] = '\0';

    if (j > 0)
      {
      fprintf(w->file, "%s%s\n", indent(w->indentation),
              vtkWrapXML_Quote(temp, 500));
      }
    else
      {
      fprintf(w->file, "\n");
      }
    }
}

/**
 * Mark the beginning of the element body
 */
void vtkWrapXML_ElementBody(wrapxml_state_t *w)
{
  if (w->unclosed)
    {
    fprintf(w->file, ">\n");
    }
  w->unclosed = 0;
}

/**
 * Print an element start tag
 */
void vtkWrapXML_ElementStart(wrapxml_state_t *w, const char *name)
{
  vtkWrapXML_ElementBody(w);
  fprintf(w->file, "%s<%s", indent(w->indentation), name);
  w->unclosed = 1;
  w->indentation++;
}

/**
 * Print an element end tag
 */
void vtkWrapXML_ElementEnd(wrapxml_state_t *w, const char *name)
{
  w->indentation--;
  if (w->unclosed)
    {
    fprintf(w->file, " />\n");
    }
  else
    {
    fprintf(w->file, "%s</%s>\n", indent(w->indentation), name);
    }
  w->unclosed = 0;
}

/**
 * Print an attribute
 */
void vtkWrapXML_Attribute(
  wrapxml_state_t *w, const char *name, const char *value)
{
#ifdef WRAPXML_ELEMENTS_ONLY
  vtkWrapXML_ElementBody(w);
  fprintf(w->file, "%s<%c%s>%s</%c%s>", indent(w->indentation),
          toupper(name[0]), &name[1], vtkWrapXML_Quote(value, 500),
          toupper(name[0]), &name[1]);
#else
  fprintf(w->file, " %s=\"%s\"", name, vtkWrapXML_Quote(value, 500));
#endif
}

/**
 * Print an attribute with a prefixed value
 */
void vtkWrapXML_AttributeWithPrefix(
  wrapxml_state_t *w, const char *name, const char *prefix, const char *value)
{
#ifdef WRAPXML_ELEMENTS_ONLY
  vtkWrapXML_ElementBody(w);
  fprintf(w->file, "%s<%c%s>%s%s</%c%s>", indent(w->indentation),
          toupper(name[0]), &name[1], prefix, vtkWrapXML_Quote(value, 500),
          toupper(name[0]), &name[1]);
#else
  fprintf(w->file, " %s=\"%s%s\"", name, prefix, vtkWrapXML_Quote(value, 500));
#endif
}

/**
 * Print a size attribute
 */
void vtkWrapXML_Size(wrapxml_state_t *w, ValueInfo *val)
{
  unsigned long ndims = val->NumberOfDimensions;
  unsigned long j;

  if (ndims > 0)
    {
#ifndef WRAPXML_ELEMENTS_ONLY
    fprintf(w->file, " size=\"%s", ((ndims > 1) ? "{" : ""));
#endif
    for (j = 0; j < ndims; j++)
      {
#ifdef WRAPXML_ELEMENTS_ONLY
      vtkWrapXML_ElementBody(w);
      fprintf(w->file, "%s<Size>%s</Size>\n", indent(w->indentation),
              val->Dimensions[j]);
#else
      fprintf(w->file, "%s%s",
        ((j > 0) ? "," : ""),
        ((val->Dimensions[j][0] == '\0') ? ":" : val->Dimensions[j]));
#endif
      }
#ifndef WRAPXML_ELEMENTS_ONLY
    fprintf(w->file, "%s\"", ((ndims > 1) ? "}" : ""));
#endif
    }
}

void vtkWrapXML_Pointer(wrapxml_state_t *w, ValueInfo *val)
{
  unsigned long ndims = val->NumberOfDimensions;
  unsigned int type = val->Type;
  unsigned int bits;
  char text[128];
  int i = 0;

  if ((type & VTK_PARSE_INDIRECT) == VTK_PARSE_BAD_INDIRECT)
    {
    vtkWrapXML_Attribute(w, "pointer", "unknown");
    return;
    }

  type = (type & VTK_PARSE_POINTER_MASK);

  if (ndims > 0)
    {
    type = ((type >> 2) & VTK_PARSE_POINTER_MASK);
    }

  while (type)
    {
    bits = (type & VTK_PARSE_POINTER_LOWMASK);
    type = ((type >> 2) & VTK_PARSE_POINTER_MASK);

    if (bits == VTK_PARSE_ARRAY)
      {
      strncpy(&text[i], "*array", 6);
      i += 6;
      }
    else if (bits == VTK_PARSE_CONST_POINTER)
      {
      strncpy(&text[i], "*const", 6);
      i += 6;
      }
    else
      {
      text[i++] = '*';
      }
    }

  if (i > 0)
    {
    text[i++] = '\0';
    vtkWrapXML_Attribute(w, "pointer", text);
    }
}

/**
 * Print the comment as multi-line text
 */
void vtkWrapXML_Comment(wrapxml_state_t *w, const char *comment)
{
  const char *elementName = "Comment";

  if (comment)
    {
    vtkWrapXML_ElementStart(w, elementName);
    vtkWrapXML_ElementBody(w);
    vtkWrapXML_MultiLineText(w, comment);
    vtkWrapXML_ElementEnd(w, elementName);
    }
}

/**
 * Print the access level
 */
void vtkWrapXML_Access(wrapxml_state_t *w, parse_access_t access)
{
  const char *cp = "public";

  switch (access)
    {
    case VTK_ACCESS_PUBLIC:
      cp = "public";
      break;
    case VTK_ACCESS_PROTECTED:
      cp = "protected";
      break;
    case VTK_ACCESS_PRIVATE:
      cp = "private";
      break;
    }

  vtkWrapXML_Attribute(w, "access", cp);
}

/**
 * Print a boolean attribute
 */
void vtkWrapXML_Flag(wrapxml_state_t *w, const char *name, int value)
{
  if (value)
    {
#ifdef WRAPXML_ELEMENTS_ONLY
    vtkWrapXML_Attribute(w, "Flag", name);
#else
    fprintf(w->file, " %s=1", name);
#endif
    }
}

/**
 * Print the name attribute
 */
void vtkWrapXML_Name(wrapxml_state_t *w, const char *name)
{
  vtkWrapXML_Attribute(w, "name", name);
}

/**
 * Print the value attribute
 */
void vtkWrapXML_Value(wrapxml_state_t *w, const char *value)
{
  vtkWrapXML_Attribute(w, "value", value);
}

/**
 * Write the file header
 */
void vtkWrapXML_FileHeader(wrapxml_state_t *w, const FileInfo *data)
{
  const char *elementName = "File";
  const char *cp = data->FileName;
  size_t i;

  vtkWrapXML_ElementStart(w, elementName);
  if (cp)
    {
    i = strlen(cp);
    while (i > 0 && cp[i-1] != '/' && cp[i-1] != '\\' && cp[i-1] != ':')
      {
      i--;
      }
    vtkWrapXML_Name(w, &cp[i]);
    }
  vtkWrapXML_ElementBody(w);
  w->indentation--;
}

/**
 * Write the file footer
 */
void vtkWrapXML_FileFooter(wrapxml_state_t *w, const FileInfo *data)
{
  const char *elementName = "File";
  /* avoid warning */
  data = NULL;

  w->indentation++;
  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Check for strings that are NULL, empty, or whitespace
 */
int vtkWrapXML_EmptyString(const char *cp)
{
  if (cp)
    {
    while (*cp != '\0')
      {
      if (!isspace(*cp++))
        {
        return 0;
        }
      }
    }

  return 1;
}

/**
 * Write out the VTK-style documentation for the file
 */
void vtkWrapXML_FileDoc(wrapxml_state_t *w, FileInfo *data)
{
  size_t n;
  char temp[500];
  const char *cp;

  if (vtkWrapXML_EmptyString(data->NameComment) &&
      vtkWrapXML_EmptyString(data->Description) &&
      vtkWrapXML_EmptyString(data->Caveats) &&
      vtkWrapXML_EmptyString(data->SeeAlso))
    {
    return;
    }

  vtkWrapXML_ElementStart(w, "Comment");
  vtkWrapXML_ElementBody(w);

  if (data->NameComment)
    {
    cp = data->NameComment;
    while (*cp == ' ')
      {
      cp++;
      }
    fprintf(w->file, "%s .NAME %s\n", indent(w->indentation),
            vtkWrapXML_Quote(cp,500));
    }

  if (data->Description)
    {
    fprintf(w->file, "\n%s .SECTION Description\n", indent(w->indentation));
    vtkWrapXML_MultiLineText(w, data->Description);
    }

  if (data->Caveats && data->Caveats[0] != '\0')
    {
    fprintf(w->file, "\n%s .SECTION Caveats\n", indent(w->indentation));
    vtkWrapXML_MultiLineText(w, data->Caveats);
    }

  if (data->SeeAlso && data->SeeAlso[0] != '\0')
    {
    fprintf(w->file, "\n%s .SECTION See also\n", indent(w->indentation));

    cp = data->SeeAlso;
    while(isspace(*cp))
      {
      cp++;
      }
    while(*cp)
      {
      n = 0;
      while(cp[n] && !isspace(cp[n]))
        {
        n++;
        }
      /* There might be another section in the See also */
      if (strncmp(cp, ".SECTION", 8) == 0)
        {
        fprintf(w->file, "\n");

        while(cp > data->SeeAlso && isspace(*(cp - 1)) && *(cp - 1) != '\n')
          {
          cp--;
          }
        vtkWrapXML_MultiLineText(w, cp);
        break;
        }

      if (n > 0 && n < 400)
        {
        strncpy(temp, cp, n);
        temp[n] = '\0';
        fprintf(w->file,"%s %s\n", indent(w->indentation),
                vtkWrapXML_Quote(temp,500));
        }
      cp += n;
      while(isspace(*cp))
        {
        cp++;
        }
      }
    }

  vtkWrapXML_ElementEnd(w, "Comment");
}

/**
 * Write the inheritance section
 */
void vtkWrapXML_ClassInheritance(
  wrapxml_state_t *w, MergeInfo *merge)
{
#ifdef WRAPXML_ELEMENTS_ONLY
  const char *elementName = "Inheritance";
  const char *subElementName = "ClassName";
#else
  const char *elementName = "ResolutionOrder";
  const char *subElementName = "Context";
#endif
  unsigned long i, n;

  /* show the geneology */
  n = merge->NumberOfClasses;

  vtkWrapXML_ElementStart(w, elementName);
  vtkWrapXML_ElementBody(w);
  for (i = 0; i < n; i++)
    {
    vtkWrapXML_ElementStart(w, subElementName);
#ifdef WRAPXML_ELEMENTS_ONLY
    vtkWrapXML_ElementBody(w);
    fprintf(w->file, "%s", vtkWrapXML_Quote(merge->ClassNames[i], 500));
#else
    vtkWrapXML_Name(w, merge->ClassNames[i]);
    vtkWrapXML_Attribute(w, "access", "public");
#endif
    vtkWrapXML_ElementEnd(w, subElementName);
    }
  vtkWrapXML_ElementEnd(w, elementName);
}

/* needed for type */
void vtkWrapXML_FunctionCommon(
  wrapxml_state_t *w, FunctionInfo *func, int doReturn);

/**
 * Print out a type in XML format
 */
void vtkWrapXML_TypeAttributes(wrapxml_state_t *w, ValueInfo *val)
{
  unsigned int type = val->Type;

#ifdef WRAPXML_ELEMENTS_ONLY
  if ((type & VTK_PARSE_CONST) != 0)
    {
    vtkWrapXML_Flag(w, "const", 1);
    }
  vtkWrapXML_Attribute(w, "type", val->Class);
#else
  if ((type & VTK_PARSE_CONST) != 0)
    {
    vtkWrapXML_AttributeWithPrefix(w, "type", "const ", val->Class);
    }
  else
    {
    vtkWrapXML_Attribute(w, "type", val->Class);
    }
#endif

  if ((type & VTK_PARSE_REF) != 0)
    {
    vtkWrapXML_Flag(w, "reference", 1);
    }

  vtkWrapXML_Pointer(w, val);
  vtkWrapXML_Size(w, val);
}

/**
 * Print out a type in XML format
 */
void vtkWrapXML_TypeElements(wrapxml_state_t *w, ValueInfo *val)
{
  if (val->Function)
    {
    if (val->Function->Class)
      {
      vtkWrapXML_ElementStart(w, "Method");
      vtkWrapXML_Attribute(w, "context", val->Function->Class);
      vtkWrapXML_FunctionCommon(w, val->Function, 1);
      vtkWrapXML_ElementEnd(w, "Method");
      }
    else
      {
      vtkWrapXML_ElementStart(w, "Function");
      vtkWrapXML_FunctionCommon(w, val->Function, 1);
      vtkWrapXML_ElementEnd(w, "Function");
      }
    }
}


/**
 * Print out a simple type types
 */
void vtkWrapXML_TypeSimple(
  wrapxml_state_t *w, unsigned int type, const char *classname, unsigned long size)
{
  char temp[256];
  char temp2[512];
  const char *sizes[2];
  ValueInfo val;

  memset(&val, 0, sizeof(ValueInfo));
  val.ItemType = VTK_VARIABLE_INFO;
  val.Type = type;

  if (classname)
    {
    strcpy(temp2, classname);
    val.Class = temp2;
    }

  sizes[0] = 0;
  sizes[1] = 0;
  if (size > 0)
    {
    sprintf(temp, "%lu", size);
    sizes[0] = temp;
    val.Dimensions = sizes;
    val.NumberOfDimensions = 1;
    }

  vtkWrapXML_TypeAttributes(w, &val);
  vtkWrapXML_TypeElements(w, &val);
}

/**
 * Print a template
 */
void vtkWrapXML_Template(
  wrapxml_state_t *w, TemplateArgs *args)
{
  const char *elementName = "TemplateArg";
  TemplateArg *arg;
  unsigned long i;

  for (i = 0; i < args->NumberOfArguments; i++)
    {
    vtkWrapXML_ElementStart(w, elementName);

    arg = args->Arguments[i];

    if (arg->Name)
      {
      vtkWrapXML_Name(w, arg->Name);
      }

    if (arg->Template)
      {
      vtkWrapXML_Attribute(w, "type", "template");
      }
    else if (arg->Type)
      {
      vtkWrapXML_Attribute(w, "type", arg->Class);
      }
    else
      {
      vtkWrapXML_Attribute(w, "type", "typename");
      }

    if (arg->Value)
      {
      vtkWrapXML_Value(w, arg->Value);
      }

    if (arg->Template)
      {
      vtkWrapXML_Flag(w, "template", 1);
      vtkWrapXML_Template(w, arg->Template);
      }

    vtkWrapXML_ElementEnd(w, elementName);
    }
}

/**
 * Print an enum
 */
void vtkWrapXML_Enum(
  wrapxml_state_t *w, EnumInfo *item, int inClass)
{
  const char *elementName = "Enum";

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);

  if (inClass)
    {
    vtkWrapXML_Access(w, item->Access);
    }

  vtkWrapXML_Name(w, item->Name);
  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Print a constant
 */
void vtkWrapXML_Constant(
  wrapxml_state_t *w, ValueInfo *con, int inClass)
{
  const char *elementName = "Constant";

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);
  
  if (inClass)
    {
    vtkWrapXML_Access(w, con->Access);
    }

  if (con->IsEnum)
    {
    vtkWrapXML_Flag(w, "enum", 1);
    }
  if (con->Type && con->Class && con->Class[0] != '\0')
    {
    vtkWrapXML_TypeAttributes(w, con);
    }
  vtkWrapXML_Name(w, con->Name);

  if (con->Value)
    {
    vtkWrapXML_Value(w, con->Value);
    }

  vtkWrapXML_Comment(w, con->Comment);
  vtkWrapXML_TypeElements(w, con);
  vtkWrapXML_ElementEnd(w, elementName);
}


/**
 * Print a variable
 */
void vtkWrapXML_Variable(
  wrapxml_state_t *w, ValueInfo *var, int inClass)
{
  const char *elementName = "Variable";

  if (inClass)
    {
    elementName = "Member";
    }

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);

  vtkWrapXML_Name(w, var->Name);

  if (inClass)
    {
    vtkWrapXML_Access(w, var->Access);
    }

  vtkWrapXML_TypeAttributes(w, var);

  if (var->Value)
    {
    vtkWrapXML_Value(w, var->Value);
    }

  vtkWrapXML_Comment(w, var->Comment);
  vtkWrapXML_TypeElements(w, var);
  vtkWrapXML_ElementEnd(w, elementName);
}


/**
 * Print a typedef
 */
void vtkWrapXML_Typedef(
  wrapxml_state_t *w, ValueInfo *type, int inClass)
{
  const char *elementName = "Typedef";

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);

  if (inClass)
    {
    vtkWrapXML_Access(w, type->Access);
    }

  if (type->Type)
    {
    vtkWrapXML_TypeAttributes(w, type);
    }

  vtkWrapXML_Name(w, type->Name);
  vtkWrapXML_Comment(w, type->Comment);
  if (type->Type)
    {
    vtkWrapXML_TypeElements(w, type);
    }

  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Print a using declaration
 */
void vtkWrapXML_Using(
  wrapxml_state_t *w, UsingInfo *data)
{
  const char *elementName = "Using";
  const char *name = "namespace";

  if (data->Name)
    {
    name = data->Name;
    }

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);
  vtkWrapXML_Name(w, name);
  vtkWrapXML_Attribute(w, "scope", data->Scope);
  vtkWrapXML_Comment(w, data->Comment);
  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Print out items that are common to functions and methods
 */
void vtkWrapXML_FunctionCommon(
  wrapxml_state_t *w, FunctionInfo *func, int printReturn)
{
  ValueInfo *arg;
  unsigned long i, n;
  char temp[500];
  const char *cp;

  if (func->IsStatic)
    {
    vtkWrapXML_Flag(w, "static", 1);
    }

  if (func->IsVariadic)
    {
    vtkWrapXML_Flag(w, "variadic", 1);
    }

  if (func->IsLegacy)
    {
    vtkWrapXML_Flag(w, "legacy", 1);
    }

  if (func->Signature)
    {
    vtkWrapXML_ElementStart(w, "Signature");
    vtkWrapXML_ElementBody(w);

    cp = func->Signature;
    for (i = 0; i < 400 && cp && cp[i] != '\0' && cp[i] != ';'; i++)
      {
      temp[i] = cp[i];
      }
    temp[i] = '\0';

    fprintf(w->file, "%s %s\n", indent(w->indentation), vtkWrapXML_Quote(temp, 500));

    vtkWrapXML_ElementEnd(w, "Signature");
  }

  vtkWrapXML_Comment(w, func->Comment);

  if (printReturn)
    {
    vtkWrapXML_ElementStart(w, "Return");
    vtkWrapXML_TypeAttributes(w, func->ReturnValue);
    vtkWrapXML_TypeElements(w, func->ReturnValue);
    vtkWrapXML_ElementEnd(w, "Return");
    }

  n = func->NumberOfArguments;
  for (i = 0; i < n; i++)
    {
    vtkWrapXML_ElementStart(w, "Arg");
    arg = func->Arguments[i];

    if (arg->Name)
      {
      vtkWrapXML_Name(w, arg->Name);
      }

    if (arg->Value)
      {
      vtkWrapXML_Value(w, arg->Value);
      }

    vtkWrapXML_TypeAttributes(w, arg);
    vtkWrapXML_TypeElements(w, arg);
    vtkWrapXML_ElementEnd(w, "Arg");
    }
}

/**
 * Print out a function in XML format
 */
void vtkWrapXML_Function(
  wrapxml_state_t *w, FunctionInfo *func)
{
  const char *elementName = "Function";

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);
  vtkWrapXML_Name(w, func->Name);

  if (func->Template)
    {
    vtkWrapXML_Flag(w, "template", 1);
    vtkWrapXML_Template(w, func->Template);
    fprintf(w->file, "\n");
    }

  vtkWrapXML_FunctionCommon(w, func, 1);
  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Print a bitfield of class property access methods
 */
void vtkWrapXML_ClassPropertyMethods(
  wrapxml_state_t *w, unsigned int methodBitfield)
{
  unsigned int i;
  unsigned int methodType;
  int first = 1;

  fprintf(w->file, " bitfield=\"");

  for (i = 0; i < 32; i++)
    {
    methodType = methodBitfield & (1U << i);
    if (methodType)
      {
      if ((methodType & VTK_METHOD_SET_CLAMP) != 0 &&
          (methodBitfield & VTK_METHOD_SET_CLAMP) == VTK_METHOD_SET_CLAMP)
        {
        methodType = VTK_METHOD_SET_CLAMP;
        methodBitfield &= ~VTK_METHOD_SET_CLAMP;
        }
      else if ((methodType & VTK_METHOD_SET_BOOL) != 0 &&
          (methodBitfield & VTK_METHOD_SET_BOOL) == VTK_METHOD_SET_BOOL)
        {
        methodType = VTK_METHOD_SET_BOOL;
        methodBitfield &= ~VTK_METHOD_SET_BOOL;
        }

      fprintf(w->file, "%s%s", ((first == 0) ? "|" : ""),
        vtkParseProperties_MethodTypeAsString(methodType));
      first = 0;
      }
    }
  fprintf(w->file, "\"");
}

/**
 * Print out a method in XML format
 */
void vtkWrapXML_ClassMethod(
  wrapxml_state_t *w, ClassInfo *data, FunctionInfo *func, const char *classname,
  const char *propname)
{
  const char *elementName = "Method";
  const char *name = func->Name;
  int isCtrOrDtr = 0;

  if (data && strcmp(data->Name, func->Name) == 0)
    {
    elementName = "Constructor";
    isCtrOrDtr = 1;
    }
  else if (data && func->Name[0] == '~' &&
           strcmp(data->Name, &func->Name[1]) == 0)
    {
    elementName = "Destructor";
    isCtrOrDtr = 1;
    }
  else if (func->IsOperator)
    {
    elementName = "Operator";
    if (strncmp(name, "operator", 8) == 0)
      {
      name = &name[8];
      while (isspace(name[0]))
        {
        name++;
        }
      }
    }

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);
  if (!isCtrOrDtr)
    {
    vtkWrapXML_Name(w, name);
    }

  if (classname)
    {
    vtkWrapXML_Attribute(w, "context", classname);
    }

  if (propname)
    {
    vtkWrapXML_Attribute(w, "property", propname);
    }

  if (func->Template)
    {
    vtkWrapXML_Flag(w, "template", 1);
    vtkWrapXML_Template(w, func->Template);
    fprintf(w->file, "\n");
    }

  vtkWrapXML_Access(w, func->Access);

  if (func->IsConst)
    {
    vtkWrapXML_Flag(w, "const", 1);
    }

  if (func->IsVirtual)
    {
    vtkWrapXML_Flag(w, "virtual", 1);
    }

  if (func->IsPureVirtual)
    {
    vtkWrapXML_Flag(w, "pure", 1);
    }

  if (func->IsExplicit)
    {
    vtkWrapXML_Flag(w, "explicit", 1);
    }

  vtkWrapXML_FunctionCommon(w, func, !isCtrOrDtr);
  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Print out a property in XML format
 */
void vtkWrapXML_ClassProperty(
  wrapxml_state_t *w, PropertyInfo *property, const char *classname)
{
  const char *elementName = "Property";
  const char *access = 0;
  unsigned long i;

  fprintf(w->file, "\n");
  vtkWrapXML_ElementStart(w, elementName);
  vtkWrapXML_Name(w, property->Name);

  if (classname)
    {
    vtkWrapXML_Attribute(w, "context", classname);
    }

  if (property->PublicMethods)
    {
    access = "public";
    }
  else if (property->ProtectedMethods)
    {
    access = "protected";
    }
  else if (property->PrivateMethods)
    {
    access = "private";
    }

  if (access)
    {
    vtkWrapXML_Attribute(w, "access", access);
    }

  if (property->IsStatic)
    {
    vtkWrapXML_Flag(w, "static", 1);
    }

  if (((property->PublicMethods | property->ProtectedMethods |
        property->PrivateMethods) & ~property->LegacyMethods) == 0)
    {
    vtkWrapXML_Flag(w, "legacy", 1);
    }

  vtkWrapXML_TypeSimple(w, property->Type, property->ClassName,
                        property->Count);

  vtkWrapXML_Comment(w, property->Comment);

  if (property->EnumConstantNames)
    {
    for (i = 0; property->EnumConstantNames[i] != 0; i++)
      {
      vtkWrapXML_ElementStart(w, "SetValueTo");
      vtkWrapXML_Attribute(w, "name", property->EnumConstantNames[i]);
      vtkWrapXML_ElementEnd(w, "SetValueTo");
      }
    }

  if (property->PublicMethods)
    {
    vtkWrapXML_ElementStart(w, "PublicMethods");
    vtkWrapXML_ClassPropertyMethods(w, property->PublicMethods);
    vtkWrapXML_ElementEnd(w, "PublicMethods");
    }

  if (property->ProtectedMethods)
    {
    vtkWrapXML_ElementStart(w, "ProtectedMethods");
    vtkWrapXML_ClassPropertyMethods(w, property->ProtectedMethods);
    vtkWrapXML_ElementEnd(w, "ProtectedMethods");
    }

  if (property->PrivateMethods)
    {
    vtkWrapXML_ElementStart(w, "PrivateMethods");
    vtkWrapXML_ClassPropertyMethods(w, property->PrivateMethods);
    vtkWrapXML_ElementEnd(w, "PrivateMethods");
    }

  if (property->LegacyMethods)
    {
    vtkWrapXML_ElementStart(w, "LegacyMethods");
    vtkWrapXML_ClassPropertyMethods(w, property->LegacyMethods);
    vtkWrapXML_ElementEnd(w, "LegacyMethods");
    }

  vtkWrapXML_ElementEnd(w, elementName);
}

/**
 * Recursive suproutine to add the methods of "classname" and all its
 * superclasses to "merge"
 */
void vtkWrapXML_MergeHelper(
  const NamespaceInfo *data, const HierarchyInfo *hinfo,
  const char *classname, FILE *hintfile, MergeInfo *info, ClassInfo *merge)
{
  FILE *fp = NULL;
  ClassInfo *cinfo = NULL;
  FileInfo *finfo = NULL;
  HierarchyEntry *entry = NULL;
  const char *header;
  const char *filename;
  unsigned long i, n;

  /* Note: this method does not deal with scoping yet.
   * "classname" might be a scoped name, in which case the
   * part before the colon indicates the class or namespace
   * (or combination thereof) where the class resides.
   * Each containing namespace or class for the "merge"
   * must be searched, taking the "using" directives that
   * have been applied into account. */

  /* find out if "classname" is in the current namespace */
  n = data->NumberOfClasses;
  for (i = 0; i < n; i++)
    {
    if (strcmp(data->Classes[i]->Name, classname) == 0)
      {
      cinfo = data->Classes[i];
      break;
      }
    }

  if (n > 0 && !cinfo)
    {
    entry = vtkParseHierarchy_FindEntry(hinfo, classname);
    if (!entry)
      {
      return;
      }
    header = entry->HeaderFile;
    filename = vtkParse_FindIncludeFile(header);
    if (!filename)
      {
      if (hintfile) { fclose(hintfile); }
      fprintf(stderr, "Couldn't locate header file %s\n", header);
      exit(1);
      }

    fp = fopen(filename, "r");
    if (!fp)
      {
      if (hintfile) { fclose(hintfile); }
      fprintf(stderr, "Couldn't open header file %s\n", header);
      exit(1);
      }

    finfo = vtkParse_ParseFile(filename, fp, stderr);
    fclose(fp);

    if (!finfo)
      {
      if (hintfile) { fclose(hintfile); }
      exit(1);
      }

    if (hintfile)
      {
      rewind(hintfile);
      vtkParse_ReadHints(finfo, hintfile, stderr);
      }

    data = finfo->Contents;

    n = data->NumberOfClasses;
    for (i = 0; i < n; i++)
      {
      if (strcmp(data->Classes[i]->Name, classname) == 0)
        {
        cinfo = data->Classes[i];
        break;
        }
      }
    }

  if (cinfo)
    {
    vtkParseMerge_Merge(info, merge, cinfo);
    n = cinfo->NumberOfSuperClasses;
    for (i = 0; i < n; i++)
      {
      vtkWrapXML_MergeHelper(data, hinfo, cinfo->SuperClasses[i],
                             hintfile, info, merge);
      }
    }
}

/**
 * Merge the methods from the superclasses
 */
MergeInfo *vtkWrapXML_MergeSuperClasses(
  NamespaceInfo *data, ClassInfo *classInfo)
{
  FILE *hintfile = NULL;
  HierarchyInfo *hinfo = NULL;
  MergeInfo *info = NULL;
  OptionInfo *oinfo = NULL;
  const char *classname;
  unsigned long i, n;

  classname = classInfo->Name;
  oinfo = vtkParse_GetCommandLineOptions();

  if (oinfo->HierarchyFileName)
    {
    hinfo = vtkParseHierarchy_ReadFile(oinfo->HierarchyFileName);

    if (oinfo->HintFileName)
      {
      hintfile = fopen(oinfo->HintFileName, "r");
      }

    info = vtkParseMerge_CreateMergeInfo(classInfo);

    n = classInfo->NumberOfSuperClasses;
    for (i = 0; i < n; i++)
      {
      vtkWrapXML_MergeHelper(data, hinfo,
                             classInfo->SuperClasses[i],
                             hintfile, info, classInfo);
      }

    if (hintfile)
      {
      fclose(hintfile);
      }
    }

  if (hinfo)
    {
    vtkParseHierarchy_Free(hinfo);
    }

  return info;
}

/**
 * Synthesize additional information before printing a method
 */
void vtkWrapXML_MethodHelper(
  wrapxml_state_t *w, MergeInfo *merge, ClassProperties *properties,
  ClassInfo *classInfo, FunctionInfo *funcInfo)
{
  const char *classname = 0;
  const char *propname = 0;
  PropertyInfo *property = NULL;
  unsigned long i, j, n;

  n = classInfo->NumberOfFunctions;

  for (i = 0; i < n; i++)
    {
    if (classInfo->Functions[i] == funcInfo)
      {
      break;
      }
    }

  if (i < n)
    {
    if (merge && merge->NumberOfOverrides[i])
      {
      classname = merge->ClassNames[merge->OverrideClasses[i][0]];
      }
    if (properties && properties->MethodHasProperty[i])
      {
      property = properties->Properties[properties->MethodProperties[i]];
      propname = property->Name;
      for (j = 0; j < i; j++)
        {
        /* only print property if this is the first occurrence */
        if (properties->MethodHasProperty[j] &&
            property ==
            properties->Properties[properties->MethodProperties[j]])
          {
          property = NULL;
          break;
          }
        }
      }
    }

  if (property)
    {
    vtkWrapXML_ClassProperty(w, property, classname);
    }

  vtkWrapXML_ClassMethod(w, classInfo, funcInfo,
                         classname, propname);
}

/**
 * Print a class as xml
 */
void vtkWrapXML_Class(
  wrapxml_state_t *w, NamespaceInfo *data, ClassInfo *classInfo, int inClass)
{
  const char *elementName = "Class";
  ClassProperties *properties;
  MergeInfo *merge = NULL;
  unsigned long i, j, n;

  /* start new XML section for class */
  fprintf(w->file, "\n");
  if (classInfo->ItemType == VTK_STRUCT_INFO)
    {
    elementName = "Struct";
    }
  else if (classInfo->ItemType == VTK_UNION_INFO)
    {
    elementName = "Union";
    }

  vtkWrapXML_ElementStart(w, elementName);

  vtkWrapXML_Name(w, classInfo->Name);

  if (inClass)
    {
    vtkWrapXML_Access(w, classInfo->Access);
    }

  if (classInfo->IsAbstract)
    {
    vtkWrapXML_Flag(w, "abstract", 1);
    }

  if (classInfo->Template)
    {
    vtkWrapXML_Flag(w, "template", 1);
    vtkWrapXML_Template(w, classInfo->Template);
    }

  vtkWrapXML_Comment(w, classInfo->Comment);

  /* actually, vtk classes never have more than one superclass */
  n = classInfo->NumberOfSuperClasses;
  for (i = 0; i < n; i++)
    {
    vtkWrapXML_ElementStart(w, "SuperClass");
    vtkWrapXML_Name(w, classInfo->SuperClasses[i]);
    vtkWrapXML_Attribute(w, "access", "public");
    vtkWrapXML_ElementEnd(w, "SuperClass");
    }

  /* merge all the superclass information */
  if (classInfo->NumberOfSuperClasses)
    {
    merge = vtkWrapXML_MergeSuperClasses(data, classInfo);
    }

  if (merge)
    {
    fprintf(w->file, "\n");
    vtkWrapXML_ClassInheritance(w, merge);
    }

  /* get information about the properties */
  properties = vtkParseProperties_Create(classInfo);

  /* print all members of the class */
  for (i = 0; i < classInfo->NumberOfItems; i++)
    {
    j = classInfo->Items[i].Index;
    switch (classInfo->Items[i].Type)
      {
      case VTK_VARIABLE_INFO:
        {
        vtkWrapXML_Variable(w, classInfo->Variables[j], 1);
        break;
        }
      case VTK_CONSTANT_INFO:
        {
        vtkWrapXML_Constant(w, classInfo->Constants[j], 1);
        break;
        }
      case VTK_ENUM_INFO:
        {
        vtkWrapXML_Enum(w, classInfo->Enums[j], 1);
        break;
        }
      case VTK_FUNCTION_INFO:
        {
        vtkWrapXML_MethodHelper(w, merge, properties, classInfo,
                                classInfo->Functions[j]);
        break;
        }
      case VTK_TYPEDEF_INFO:
        {
        vtkWrapXML_Typedef(w, classInfo->Typedefs[j], 1);
        break;
        }
      case VTK_USING_INFO:
        {
        vtkWrapXML_Using(w, classInfo->Usings[j]);
        break;
        }
      case VTK_CLASS_INFO:
      case VTK_STRUCT_INFO:
      case VTK_UNION_INFO:
        {
        vtkWrapXML_Class(w, data, classInfo->Classes[j], 1);
        break;
        }
      case VTK_NAMESPACE_INFO:
        break;
      }
    }

  /* release the information about the properties */
  vtkParseProperties_Free(properties);

  /* release the info about what was merged from superclasses */
  if (merge)
    {
    vtkParseMerge_FreeMergeInfo(merge);
    }

  vtkWrapXML_ElementEnd(w, elementName);
}

/* needed for vtkWrapXML_Body */
void vtkWrapXML_Namespace(wrapxml_state_t *w, NamespaceInfo *data);

/**
 * Print the body of a file or namespace
 */
void vtkWrapXML_Body(wrapxml_state_t *w, NamespaceInfo *data)
{
  unsigned long i, j;

  /* print all constants for the file or namespace */
  for (i = 0; i < data->NumberOfItems; i++)
    {
    j = data->Items[i].Index;
    switch (data->Items[i].Type)
      {
      case VTK_VARIABLE_INFO:
        {
        vtkWrapXML_Variable(w, data->Variables[j], 0);
        break;
        }
      case VTK_CONSTANT_INFO:
        {
        vtkWrapXML_Constant(w, data->Constants[j], 0);
        break;
        }
      case VTK_TYPEDEF_INFO:
        {
        vtkWrapXML_Typedef(w, data->Typedefs[j], 0);
        break;
        }
      case VTK_USING_INFO:
        {
        vtkWrapXML_Using(w, data->Usings[j]);
        break;
        }
      case VTK_ENUM_INFO:
        {
        vtkWrapXML_Enum(w, data->Enums[j], 0);
        break;
        }
      case VTK_CLASS_INFO:
      case VTK_STRUCT_INFO:
      case VTK_UNION_INFO:
        {
        vtkWrapXML_Class(w, data, data->Classes[j], 0);
        break;
        }
      case VTK_FUNCTION_INFO:
        {
        vtkWrapXML_Function(w, data->Functions[j]);
        break;
        }
      case VTK_NAMESPACE_INFO:
        {
        vtkWrapXML_Namespace(w, data->Namespaces[j]);
        break;
        }
      }
    }
}

/**
 * Print a namespace as xml
 */
void vtkWrapXML_Namespace(wrapxml_state_t *w, NamespaceInfo *data)
{
  fprintf(w->file, "\n");
  fprintf(w->file, "%s<Namespace>\n", indent(w->indentation++));
  fprintf(w->file, "%s<Name>%s</Name>\n", indent(w->indentation), data->Name);
  vtkWrapXML_Body(w, data);
  fprintf(w->file, "\n");
  fprintf(w->file, "%s</Namespace>\n", indent(--w->indentation));
}

/**
 * Main function that takes a parsed FileInfo from vtk and produces a
 * specific vtkXML format for desired functions to be incorporated in SimVTK
 * (ie. certain add, remove, get and set methods).
 *
 * This method is called from vtkParseMain.c
 */
void vtkParseOutput(FILE *fp, FileInfo *data)
{
  wrapxml_state_t ws;
  ws.file = fp;
  ws.indentation = 0;
  ws.unclosed = 0;

  /* print the lead-in */
  vtkWrapXML_FileHeader(&ws, data);

  /* print the documentation */
  vtkWrapXML_FileDoc(&ws, data);

  /* print the main body */
  vtkWrapXML_Body(&ws, data->Contents);

  /* print the closing tag */
  vtkWrapXML_FileFooter(&ws, data);
}
