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

static Config initConfig(ConfigType type, char* element, char* pointerElement, char* attribute, char* pointer) {
    Config config = (Config) malloc(sizeof(struct config));

    config->type = type;
    config->element = strdup(element);
    config->pointerElement = strdup(pointerElement);
    config->attribute = strdup(attribute);
    config->pointer = strdup(pointer);

    return config;
}


static void deleteConfig(Config config) {
    free(config->element);
    free(config->pointerElement);
    free(config->attribute);
    free(config->pointer);
    free(config);
}


static void printConfig(Config config) {
    cout << asString(config->type) << endl;
    cout << config->element << endl;
    cout << config->pointerElement << endl;
    cout << config->attribute << endl;
    cout << config->pointer << endl << endl;
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


static void insertConfig(Configs configs, string typeS, string element, string pointerElement, string attribute, string pointer) {
    ConfigType type = typeMap(typeS);

    if(configs->used == 0)
        configs->configs = (Config*) malloc(sizeof(Config));

    else if(configs->used == configs->dynamic_size) {
        configs->dynamic_size *= 2;
        configs->configs = (Config*) realloc(configs->configs, configs->dynamic_size * sizeof(Config));
    }

    configs->configs[configs->used++] = initConfig(type, (char*) element.c_str(), (char*) pointerElement.c_str(),
                                                   (char*) attribute.c_str(), (char*) pointer.c_str());
}


vector<tuple<string, string, bool>> getAttributesTypes(Configs configs, string elementName) {
    vector<tuple<string, string, bool>> result;
    bool IO;
    
    for(int i = 0; i < configs->used; i++) {
        if(compareConfig(configs->configs[i], (char*) elementName.c_str(), true, &IO))
            result.push_back(make_tuple((IO ? getInput(configs->configs[i]) : getOutput(configs->configs[i])), 
                                         getType(configs->configs[i]), IO));

        if(compareConfig(configs->configs[i], (char*) elementName.c_str(), false, &IO))
            result.push_back(make_tuple((IO ? getInput(configs->configs[i]) : getOutput(configs->configs[i])), 
                                         getType(configs->configs[i]), IO));
    }

    return result;
};


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


static bool validateConfig(vector<vector<string>> tokens, vector<string> & errors) {
    ConfigType type;
    string pointer;
    vector<tuple<string, string>> combinations;

    for(int i = 0; i < tokens.size(); i++) {
        if(tokens.at(i).size() != 5) {
            errors.push_back(string("Config error: Wrong number of fields in line ") + 
                             to_string(i + 1) + string(".\n"));
            return false;
        }

        if((tokens.at(i).at(1) + tokens.at(i).at(2)).compare(tokens.at(i).at(3) + tokens.at(i).at(4)) == 0) {
            errors.push_back(string("Config error: An attribute can't be an pointer to himself in line ") + 
                             to_string(i + 1) + string(".\n"));
            return false;
        }

        
        for(int j = 0; j < tokens.size(); j++) {
            if(j == i)
                continue;

            if((tokens.at(i).at(1) + tokens.at(i).at(2)).compare(string("__")) != 0 && 
               (((tokens.at(i).at(1) + tokens.at(i).at(2)).compare((tokens.at(j).at(1) + tokens.at(j).at(2))) == 0) || 
               ((tokens.at(i).at(1) + tokens.at(i).at(2)).compare((tokens.at(j).at(3) + tokens.at(j).at(4))) == 0))) {

                errors.push_back(string("Config error: In Attribute repeated in lines ") + to_string(i + 1) + 
                                 string(" and ") + to_string(j + 1) + string(".\n"));
            
                return false;
            }

            
            if((tokens.at(i).at(3) + tokens.at(i).at(4)).compare(string("__")) != 0 && 
               (((tokens.at(i).at(3) + tokens.at(i).at(4)).compare((tokens.at(j).at(1) + tokens.at(j).at(2))) == 0) || 
               ((tokens.at(i).at(3) + tokens.at(i).at(4)).compare((tokens.at(j).at(3) + tokens.at(j).at(4))) == 0))) {
                
                errors.push_back(string("Config error: Out Attribute repeated in lines ") + to_string(i + 1) + 
                                 string(" and ") + to_string(j + 1) + string(".\n"));
            
                return false;
            }
        }
    }

    return true;
}


bool loadConfig(Configs configs, string path, vector<string> & errors) {
    FILE* fp = fopen(path.c_str(), "r");
    int tam = 100;
    char buffer[tam];
    vector<vector<string>> tokens;

    while(fgets(buffer, tam, fp))
        tokens.push_back(split(buffer, regex("\\s+")));

    if(!validateConfig(tokens, errors))
        return false;

    for(int i = 0; i < tokens.size(); i++)
        insertConfig(configs, tokens.at(i).at(0), tokens.at(i).at(1), tokens.at(i).at(3), tokens.at(i).at(2), tokens.at(i).at(4));

    return true;
}