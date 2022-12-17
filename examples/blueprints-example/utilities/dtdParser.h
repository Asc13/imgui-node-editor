#ifndef Parser_H
#define Parser_H


#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <stdbool.h>
#include <ctype.h>
#include <regex>
#include <sstream>
#include <vector>
#include <iostream>


using namespace std;


typedef struct document* DocumentDTD;
typedef struct element* ElementDTD;
typedef struct attributeList* AttributeListDTD;

DocumentDTD initDocument();

void parseDocument(DocumentDTD document, string path);

#endif