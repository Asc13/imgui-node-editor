#ifndef DTDPARSER_H
#define DTDPARSER_H


#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <stdbool.h>
#include <ctype.h>
#include <regex>
#include <sstream>
#include <vector>
#include <set>
#include <iostream>
#include <algorithm>


using namespace std;


typedef struct document* DocumentDTD;
typedef struct rule* RuleDTD;
typedef struct element* ElementDTD;
typedef struct attributeList* AttributeListDTD;


DocumentDTD initDocument();

void parseDocument(DocumentDTD document, string path);

void validateAttributes(DocumentDTD document, set<string> & errors, string elementName, 
                        vector<string> attributeName, vector<string> attributeValue, bool* ok);

void validateElements(DocumentDTD document, set<string> & errors, string elementName, 
                      vector<string> childs, bool* ok);

void deleteDocument(DocumentDTD document);

#endif