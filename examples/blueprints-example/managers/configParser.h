#ifndef CONFIGPARSER_H
#define CONFIGPARSER_H


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


using namespace std;

typedef struct configs* Configs;
typedef struct config* Config;

vector<string> split(string configLine, regex rgx);

vector<tuple<string, string, bool>> getAttributesTypes(Configs configs, string elementName);

Configs initConfigs();

void deleteConfigs(Configs configs);

void parseConfig(Configs configs, string path);

void validateAttributesByElement(Configs configs, string elementName, vector<tuple<string, string>> nameValue, set<string> & errors, bool* ok);

vector<vector<string>> getLinks(Configs configs);

string getPointer(Configs configs, string element, set<int> & used);

tuple<string, string> getConfigByIndex(Configs configs, string element, int index);

bool isValidLink(Configs configs, string name, string pointer);

bool loadConfig(Configs configs, string path, set<string> & errors);

#endif