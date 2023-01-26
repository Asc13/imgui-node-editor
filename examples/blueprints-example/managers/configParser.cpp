#include "configParser.h"

enum class ConfigType {
    Bool,
    Int,
    Float,
    Double,
    String,
    Null
};


struct configs {
    Config* configs;
    int used;
    int dynamic_size;
};


struct config {
    ConfigType type;
    char* element;
    char* pointerElement;
    char* attribute;
    char* pointer;
    char* rule;
};


vector<string> split(string configLine, regex rgx) {
    vector<string> result;

    sregex_token_iterator iter(configLine.begin(),
                               configLine.end(),
                               rgx, -1);

    sregex_token_iterator end;   

    while(iter != end) {
        if(iter->length())
            result.push_back(*iter);
        iter++;
    }

    return result;
}


static ConfigType typeMap(string typeS) {
    if(regex_match(typeS, regex("(Bool|bool|BOOL|Boolean|boolean|BOOLEAN)")))
        return ConfigType::Bool;

    if(regex_match(typeS, regex("(Int|int|INT|Integer|integer|INTEGER)")))
        return ConfigType::Int;

    if(regex_match(typeS, regex("(Float|float|FLOAT)")))
        return ConfigType::Float;

    if(regex_match(typeS, regex("(Double|double|DOUBLE)")))
        return ConfigType::Double;

    if(regex_match(typeS, regex("(String|string|STRING|TEXT)")))
        return ConfigType::String;

    else
        return ConfigType::Null;
}


static string asString(ConfigType type) {
    switch(type) {
        case ConfigType::Bool:
            return string("Bool");
            break;
        
        case ConfigType::Int:
            return string("Int");
            break;

        case ConfigType::Float:
            return string("Float");
            break;

        case ConfigType::Double:
            return string("Double");
            break;

        case ConfigType::String:
            return string("String");
            break;

        default:
            return string("Null");
            break;
    }
}

static Config initConfig(ConfigType type, char* element, char* pointerElement, char* attribute, char* pointer, char* rule) {
    Config config = (Config) malloc(sizeof(struct config));

    config->type = type;
    config->element = strdup(element);
    config->pointerElement = strdup(pointerElement);
    config->attribute = strdup(attribute);
    config->pointer = strdup(pointer);
    config->rule = strdup(rule);

    return config;
}


static void deleteConfig(Config config) {
    free(config->element);
    free(config->pointerElement);
    free(config->attribute);
    free(config->pointer);
    free(config->rule);
    free(config);
}


static void printConfig(Config config) {
    cout << asString(config->type) << endl;
    cout << config->element << endl;
    cout << config->pointerElement << endl;
    cout << config->attribute << endl;
    cout << config->pointer << endl;
    cout << config->rule << endl << endl;
}


static bool isLink(Config config) {
    return (strcmp(config->attribute, "_") != 0 && strcmp(config->pointer, "_") != 0); 
}


static string getInput(Config config) {
    return string(config->attribute);
}


static string getOutput(Config config) {
    return string(config->pointer);
}


static string getType(Config config) {
    return asString(config->type);
}


static bool compareConfig(Config config, char* name, bool flag, bool* IO) {
    if(flag && strcmp(name, config->element) == 0 && 
               strcmp("_", config->attribute) != 0) {
        *IO = true;
        return true;
    }
    
    if(!flag && strcmp(name, config->pointerElement) == 0 && 
                strcmp("_", config->pointer) != 0) {
        *IO = false;
        return true;
    }

    return false;
}


static void insertConfig(Configs configs, string typeS, string element, string pointerElement, string attribute, 
                         string pointer, string rule) {
    ConfigType type = typeMap(typeS);

    if(configs->used == 0)
        configs->configs = (Config*) malloc(sizeof(Config));

    else if(configs->used == configs->dynamic_size) {
        configs->dynamic_size *= 2;
        configs->configs = (Config*) realloc(configs->configs, configs->dynamic_size * sizeof(Config));
    }

    configs->configs[configs->used++] = initConfig(type, (char*) element.c_str(), (char*) pointerElement.c_str(),
                                                   (char*) attribute.c_str(), (char*) pointer.c_str(),
                                                   (char*) rule.c_str());
}

template<typename Numeric>
bool isNumber(string value) {
    try {
        stoi(value) || stof(value) || stod(value);
        Numeric n;
        return (istringstream(value) >> n >> ws).eof();
    }
    catch (...) {
        return false;
    }
}


static bool isBoolean(string value) {
    return regex_match(value, regex("(1|0|True|False|true|false)"));
}


static void validateValue(Config config, string value, string element, string attribute, set<string> & errors, bool* ok) {
    if(strcmp(config->rule, "_") == 0)
        return;

    if(config->type == ConfigType::String && stoi(string(config->rule)) < value.size()) {
        errors.insert(string("The string size must be lower or equal to " + string(config->rule) + 
                             " on attribute " + attribute + " from " +  element + " Node!"));
        *ok = false;     
        return;
    }

    vector<string> ruleFields;

    if(config->type == ConfigType::Int) {
        if(isNumber<int>(value) || isNumber<long int>(value)) {
            ruleFields = split(string(config->rule), regex(","));

            if((ruleFields.at(0).at(0) == '[' && stoi(value) < stoi(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(0).at(0) == ']' && stoi(value) <= stoi(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(1).back() == '['&& stoi(value) >= stoi(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2))) ||
            (ruleFields.at(1).back() == ']' && stoi(value) > stoi(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2)))) {
                errors.insert(string("The integer don't respect the interval " + string(config->rule) +
                                     " on attribute " + attribute + " from " +  element + " Node!"));
                *ok = false;
                return;
            }

            
        }

        else {
            errors.insert(string("The value of attribute " + attribute + " from " + 
                                 element + " Node must be an integer!"));
            *ok = false;
            return;
        }
    }
    
    if(config->type == ConfigType::Float) {
        if(isNumber<float>(value)) {
            ruleFields = split(string(config->rule), regex(","));

            if((ruleFields.at(0).at(0) == '[' && stof(value) < stof(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(0).at(0) == ']' && stof(value) <= stof(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(1).back() == '['&& stof(value) >= stof(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2))) ||
            (ruleFields.at(1).back() == ']' && stof(value) > stof(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2)))) {

                errors.insert(string("The float don't respect the interval " + string(config->rule) +
                                    " on attribute " + attribute + " from " +  element + " Node!"));
                *ok = false;
                return;
            }
        }

        else {
            errors.insert(string("The value of attribute " + attribute + " from " + 
                                 element + " Node must be a float!"));
            *ok = false;
            return;
        }
    }

    if(config->type == ConfigType::Double) {
        if(isNumber<double>(value) || isNumber<long double>(value)) {
            ruleFields = split(string(config->rule), regex(","));

            if((ruleFields.at(0).at(0) == '[' && stod(value) < stod(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(0).at(0) == ']' && stod(value) <= stod(ruleFields.at(0).substr(1))) ||
            (ruleFields.at(1).back() == '['&& stod(value) >= stod(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2))) ||
            (ruleFields.at(1).back() == ']' && stod(value) > stod(ruleFields.at(1).substr(0, ruleFields.at(1).size() - 2)))) {
                errors.insert(string("The double don't respect the interval " + string(config->rule) +
                                    " on attribute " + attribute + " from " +  element + " Node!"));
                *ok = false;
                return;
            }
        }

        else {
            errors.insert(string("The value of attribute " + attribute + " from " + 
                                 element + " Node must be a double!"));
            *ok = false;
            return;
        }
    }

    if(config->type == ConfigType::Bool && !isBoolean(value)) {
        errors.insert(string("The value of attribute " + attribute + " from " + 
                             element + " Node must be a boolean!"));
        *ok = false;
        return;
    }
}


static void validateAttributes(Config config, vector<tuple<string, string>> nameValue, string element, set<string> & errors, bool* ok, bool pointer) {
    for(auto & vt : nameValue) {
        if(!pointer && strcmp(config->attribute, (char*) get<0>(vt).c_str()) == 0)
            validateValue(config, get<1>(vt), element, get<0>(vt), errors, ok);

        if(pointer && strcmp(config->pointer, (char*) get<0>(vt).c_str()) == 0)
            validateValue(config, get<1>(vt), element, get<0>(vt), errors, ok);
    }
}


void validateAttributesByElement(Configs configs, string elementName, vector<tuple<string, string>> nameValue, set<string> & errors, bool* ok) {
    bool pointer;

    for(int i = 0; i < configs->used; i++)
        if((strcmp(configs->configs[i]->element, (char*) elementName.c_str()) == 0) ^
           (pointer = strcmp(configs->configs[i]->pointerElement, (char*) elementName.c_str()) == 0))
            validateAttributes(configs->configs[i], nameValue, elementName, errors, ok, pointer);
}


vector<tuple<string, string, bool>> getAttributesTypes(Configs configs, string elementName) {
    vector<tuple<string, string, bool>> result;
    bool IO;
    
    for(int i = 0; i < configs->used; i++) {
        if(compareConfig(configs->configs[i], (char*) elementName.c_str(), true, &IO))
            result.push_back(make_tuple((IO ? getInput(configs->configs[i]) : getOutput(configs->configs[i])), 
                                         getType(configs->configs[i]), !isLink(configs->configs[i]) ? IO : !IO));

        if(compareConfig(configs->configs[i], (char*) elementName.c_str(), false, &IO))
            result.push_back(make_tuple((IO ? getInput(configs->configs[i]) : getOutput(configs->configs[i])), 
                                         getType(configs->configs[i]), !isLink(configs->configs[i]) ? IO : !IO));
    }

    return result;
}


Configs initConfigs() {
    Configs configs = (Configs) malloc(sizeof(struct configs));

    configs->used = 0;
    configs->dynamic_size = 1;

    return configs;
}


void deleteConfigs(Configs configs) {
    if(configs->used > 0) {
        for(int i = 0; i < configs->used; i++)
            deleteConfig(configs->configs[i]);

        free(configs->configs);
        configs->configs = NULL;
        configs->used = 0;
        configs->dynamic_size = 1;
    }
}


static bool validateConfig(vector<vector<string>> tokens, set<string> & errors) {
    ConfigType type;
    string pointer;
    vector<tuple<string, string>> combinations;

    for(int i = 0; i < tokens.size(); i++) {
        if(tokens.at(i).size() != 6) {
            errors.insert(string("Config error: Wrong number of fields in line ") + 
                             to_string(i + 1) + string(".\n"));
            return false;
        }

        if((tokens.at(i).at(1) + tokens.at(i).at(2)).compare(tokens.at(i).at(3) + tokens.at(i).at(4)) == 0) {
            errors.insert(string("Config error: An attribute can't be an pointer to himself in line ") + 
                             to_string(i + 1) + string(".\n"));
            return false;
        }

        
        for(int j = 0; j < tokens.size(); j++) {
            if(j == i)
                continue;

            if((tokens.at(i).at(1) + tokens.at(i).at(2)).compare(string("__")) != 0 && 
               (((tokens.at(i).at(1) + tokens.at(i).at(2)).compare((tokens.at(j).at(1) + tokens.at(j).at(2))) == 0) || 
               ((tokens.at(i).at(1) + tokens.at(i).at(2)).compare((tokens.at(j).at(3) + tokens.at(j).at(4))) == 0))) {

                errors.insert(string("Config error: In Attribute repeated in lines ") + to_string(i + 1) + 
                              string(" and ") + to_string(j + 1) + string(".\n"));
            
                return false;
            }

            
            if((tokens.at(i).at(3) + tokens.at(i).at(4)).compare(string("__")) != 0 && 
               (((tokens.at(i).at(3) + tokens.at(i).at(4)).compare((tokens.at(j).at(1) + tokens.at(j).at(2))) == 0) || 
               ((tokens.at(i).at(3) + tokens.at(i).at(4)).compare((tokens.at(j).at(3) + tokens.at(j).at(4))) == 0))) {
                
                errors.insert(string("Config error: Out Attribute repeated in lines ") + to_string(i + 1) + 
                              string(" and ") + to_string(j + 1) + string(".\n"));
            
                return false;
            }

            if(tokens.at(i).at(5).compare("_") != 0) {
                vector<string> rule = split(tokens.at(i).at(5), regex("(\\[|,|\\])"));

                if(typeMap(tokens.at(i).at(0)) == ConfigType::Int && 
                  (!isNumber<int>(rule.at(0)) || !isNumber<long int>(rule.at(0)) ||
                  !isNumber<int>(rule.at(1)) || !isNumber<long int>(rule.at(1)))) {

                    errors.insert(string("Config error: The type interval ") + tokens.at(i).at(5) + string("in line ") + to_string(i + 1) + string(" must contain integers!!"));
                    
                    return false;
                }

                if(typeMap(tokens.at(i).at(0)) == ConfigType::Float && 
                  (!isNumber<float>(rule.at(0))) || !isNumber<float>(rule.at(1))) {

                    errors.insert(string("Config error: The type interval ") + tokens.at(i).at(5) + string("in line ") + to_string(i + 1) + string(" must contain float!!"));
                    
                    return false;
                }

                if(typeMap(tokens.at(i).at(0)) == ConfigType::Double && 
                  (!isNumber<double>(rule.at(0)) || !isNumber<long double>(rule.at(0)) ||
                   !isNumber<double>(rule.at(1)) || !isNumber<long double>(rule.at(1)))) {

                    errors.insert(string("Config error: The type interval ") + tokens.at(i).at(5) + string("in line ") + to_string(i + 1) + string(" must contain double!!"));
                    
                    return false;
                }
            }
        }
    }

    return true;
}


vector<vector<string>> getLinks(Configs configs) {
    vector<vector<string>> links;
    vector<string> temp;

    for(int i = 0; i < configs->used; i++)
        if(isLink(configs->configs[i])) {
            temp.push_back(configs->configs[i]->element);
            temp.push_back(configs->configs[i]->attribute);
            temp.push_back(configs->configs[i]->pointerElement);
            temp.push_back(configs->configs[i]->pointer);
            temp.push_back(getType(configs->configs[i]));
            links.push_back(temp);
            temp.clear();
        }

    return links;
}


string getPointer(Configs configs, string element, set<int> & used) {
    for(int i = 0; i < configs->used; ++i) {
        if(isLink(configs->configs[i]) && !used.count(i)) {
            if(strcmp(configs->configs[i]->element, element.c_str()) == 0) {
                used.insert(i);
                return configs->configs[i]->pointerElement;
            }
        
            if(strcmp(configs->configs[i]->pointerElement, element.c_str()) == 0) {
                used.insert(i);
                return configs->configs[i]->element;
            }
        }

    }

    return string("");
}


tuple<string, string> getConfigByIndex(Configs configs, string element, int index) {
    return (element.compare(configs->configs[index]->element) == 0) ? 
           make_tuple(configs->configs[index]->pointerElement, configs->configs[index]->pointer) :
           make_tuple(configs->configs[index]->element, configs->configs[index]->attribute);
}


bool isValidLink(Configs configs, string name, string pointer) {
    for(int i = 0; i < configs->used; i++)
        if(strcmp(configs->configs[i]->attribute, name.c_str()) == 0 &&
           strcmp(configs->configs[i]->pointer, pointer.c_str()) == 0)
            
            return true;

    return false;
}


bool loadConfig(Configs configs, string path, set<string> & errors) {
    FILE* fp = fopen(path.c_str(), "r");
    int tam = 100;
    char buffer[tam];
    vector<vector<string>> tokens;

    while(fgets(buffer, tam, fp))
        tokens.push_back(split(buffer, regex("\\s+")));

    if(!validateConfig(tokens, errors))
        return false;

    for(int i = 0; i < tokens.size(); i++)
        insertConfig(configs, tokens.at(i).at(0), tokens.at(i).at(1), tokens.at(i).at(3), tokens.at(i).at(2), tokens.at(i).at(4), tokens.at(i).at(5));

    return true;
}