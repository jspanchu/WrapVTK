/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkParseMain.c

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

/*

This file provides a unified front-end for the wrapper generators.
It contains the main() function and argument parsing, and calls
the code that parses the header file.

*/

#include "vtkParse.h"
#include "vtkParseData.h"
#include "vtkParseMain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* This is the struct that contains the options */
OptionInfo options;

/* Check the options */
static int check_options(int argc, char *argv[])
{
  int i;
  size_t j;

  options.InputFileName = NULL;
  options.OutputFileName = NULL;
  options.IsAbstract = 0;
  options.IsConcrete = 0;
  options.IsVTKObject = 0;
  options.IsSpecialObject = 0;
  options.HierarchyFileName = 0;
  options.HintFileName = 0;

  for (i = 1; i < argc && argv[i][0] == '-'; i++)
    {
    if (strcmp(argv[i], "--concrete") == 0)
      {
      options.IsConcrete = 1;
      }
    else if (strcmp(argv[i], "--abstract") == 0)
      {
      options.IsAbstract = 1;
      }
    else if (strcmp(argv[i], "--vtkobject") == 0)
      {
      options.IsVTKObject = 1;
      }
    else if (strcmp(argv[i], "--special") == 0)
      {
      options.IsSpecialObject = 1;
      }
    else if (strcmp(argv[i], "--hints") == 0)
      {
      i++;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      options.HintFileName = argv[i];
      }
    else if (strcmp(argv[i], "--types") == 0)
      {
      i++;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      options.HierarchyFileName = argv[i];
      }
    else if (strcmp(argv[i], "-o") == 0)
      {
      i++;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      options.OutputFileName = argv[i];
      }
    else if (strcmp(argv[i], "-I") == 0)
      {
      i++;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      vtkParse_IncludeDirectory(argv[i]);
      }
    else if (strcmp(argv[i], "-D") == 0)
      {
      i++;
      j = 0;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      while (argv[i][j] != '\0' && argv[i][j] != '=') { j++; }
      if (argv[i][j] == '=') { j++; }
      vtkParse_DefineMacro(argv[i], &argv[i][j]);
      }
    else if (strcmp(argv[i], "-U") == 0)
      {
      i++;
      if (i >= argc || argv[i][0] == '-')
        {
        return -1;
        }
      vtkParse_UndefineMacro(argv[i]);
      }
    }

  return i;
}

/* Return a pointer to the static OptionInfo struct */
OptionInfo *vtkParse_GetCommandLineOptions()
{
  return &options;
}

FileInfo *vtkParse_Main(int argc, char *argv[])
{
  int argi;
  int has_options = 0;
  FILE *ifile;
  FILE *hfile = 0;
  const char *cp;
  char *classname;
  size_t i;
  FileInfo *data;

  argi = check_options(argc, argv);
  if (argi > 1 && argc - argi == 1)
    {
    has_options = 1;
    }
  else if (argi < 0 || argc < 3 || argc > 5)
    {
    fprintf(stderr,
            "Usage: %s [options] input_file\n"
            "  -o <file>       the output file\n"
            "  -I <dir>        add an include directory\n"
            "  -D <macro>      add a macro definition\n"
            "  -U <macro>      undefine a macro\n"
            "  --concrete      force concrete class\n"
            "  --abstract      force abstract class\n"
            "  --vtkobject     vtkObjectBase-derived class\n"
            "  --special       non-vtkObjectBase class\n"
            "  --hints <file>  hints file\n"
            "  --types <file>  type hierarchy file\n",
            argv[0]);
    exit(1);
    }

  options.InputFileName = argv[argi++];

  if (!(ifile = fopen(options.InputFileName, "r")))
    {
    fprintf(stderr,"Error opening input file %s\n", options.InputFileName);
    exit(1);
    }

  if (!has_options)
    {
    if (argc == 5)
      {
      options.HintFileName = argv[argi++];
      }
    if (argc >= 4)
      {
      options.IsConcrete = atoi(argv[argi++]);
      options.IsAbstract = !options.IsConcrete;
      }
    options.OutputFileName = argv[argi++];
    }

  if (options.HintFileName && options.HintFileName[0] != '\0')
    {
    if (!(hfile = fopen(options.HintFileName, "r")))
      {
      fprintf(stderr, "Error opening hint file %s\n", options.HintFileName);
      fclose(ifile);
      exit(1);
      }
    }

  if (options.OutputFileName == NULL)
    {
    fprintf(stderr, "No output file was specified\n");
    fclose(ifile);
    if (hfile)
      {
      fclose(hfile);
      }
    exit(1);
    }

  if (options.IsConcrete)
    {
    cp = options.InputFileName;
    i = strlen(cp);
    classname = (char *)malloc(i+1);
    while (i > 0 &&
           cp[i-1] != '/' && cp[i-1] != '\\' && cp[i-1] != ':') { i--; }
    strcpy(classname, &cp[i]);
    i = 0;
    while (classname[i] != '\0' && classname[i] != '.') { i++; }
    classname[i] = '\0';

    vtkParse_SetClassProperty(classname, "concrete");
    }

  vtkParse_SetIgnoreBTX(0);
  if (options.HierarchyFileName)
    {
    vtkParse_SetIgnoreBTX(1);
    }

  data = vtkParse_ParseFile(options.InputFileName, ifile, stderr);

  if (data && hfile)
    {
    vtkParse_ReadHints(data, hfile, stderr);
    }

  if (!data)
    {
    exit(1);
    }

  if (options.IsConcrete && data->MainClass)
    {
    data->MainClass->IsAbstract = 0;
    }
  else if (options.IsAbstract && data->MainClass)
    {
    data->MainClass->IsAbstract = 1;
    }

  return data;
}
