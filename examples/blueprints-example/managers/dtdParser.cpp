#include "dtdParser.h"

struct document {
    char* source;
    char* root;
    ElementDTD* elementList;
    int elementsUsed, elementsSize;
    int attributeListsUsed, attributeListsSize;
    AttributeListDTD* attributeList;
};


struct rule {
    char** childs;
    int childsUsed, childsSize;
    char rule; // , or | or _
    char regex; // * or + or ? or _
};


struct element {
    char* elementName;
    RuleDTD* rules;
    int rulesUsed, rulesSize;
};


struct attributeList {
    char* elementName;
    char** attributeName;
    char** attributeType;
    char** attributeOption;
    int attributesUsed, attributesSize;
};


static vector<string> split(string attributeListString, regex rgx) {
    vector<string> result;

    sregex_token_iterator iter(attributeListString.begin(),
                               attributeListString.end(),
                               rgx, -1);

    sregex_token_iterator end;   

    while(iter != end) {
        if(iter->length())
            result.push_back(*iter);
        iter++;
    }

    return result;
}


static AttributeListDTD initAttributeList() {
    AttributeListDTD attributeList = (AttributeListDTD) malloc(sizeof(struct attributeList));

    attributeList->attributesUsed = 0;
    attributeList->attributesSize = 1;

    return attributeList;
}


static RuleDTD initRule(char r, char rgx) {
    RuleDTD rule = (RuleDTD) malloc(sizeof(struct rule));

    rule->childsUsed = 0;
    rule->childsSize = 1;
    rule->rule = r;
    rule->regex = rgx;

    return rule;
}



static void printRules(ElementDTD element) {
    cout << element->elementName << ":" << endl;

    for(int i = 0; i < element->rulesUsed; i++) {
        cout << element->rules[i]->regex << " " << element->rules[i]->rule << endl;

        for(int j = 0; j < element->rules[i]->childsUsed; j++)
            cout << (j != 0 ? " | " : "") << element->rules[i]->childs[j];
    
        cout << endl;
    }

    cout << endl;
}

static void addRule(RuleDTD rule, char* child) {
    if(rule->childsUsed == 0)
        rule->childs = (char**) malloc(sizeof(char*));

    else if(rule->childsUsed == rule->childsSize) {
        rule->childsSize *= 2;
        rule->childs = (char**) realloc(rule->childs, rule->childsSize * sizeof(char*));
    }
    
    rule->childs[rule->childsUsed++] = strdup(child);
}


static ElementDTD initElement(char* elementName) {
    ElementDTD element = (ElementDTD) malloc(sizeof(struct element));

    element->rulesUsed = 0;
    element->rulesSize = 1;
    element->elementName = strdup(elementName);

    return element;
}


static void addElementRule(ElementDTD element, RuleDTD rule) {
    if(element->rulesUsed == 0)
        element->rules = (RuleDTD*) malloc(sizeof(RuleDTD));

    else if(element->rulesUsed == element->rulesSize) {
        element->rulesSize *= 2;
        element->rules = (RuleDTD*) realloc(element->rules, element->rulesSize * sizeof(RuleDTD));
    }
    
    element->rules[element->rulesUsed++] = rule;
}


static void parseChilds(ElementDTD element, string childs) {
    vector<string> tokens;

    regex ORrgx = regex("\\([^\\|,]+(\\|[^\\|,]+)+\\)[*|+|?]?"),
          ANDrgx = regex("\\([^\\,]+(,[^\\,]+)*\\)");

    if(childs.compare("EMPTY") == 0)
        addElementRule(element, initRule('E', '_'));

    else if(childs.compare("ANY") == 0)
        addElementRule(element, initRule('A', '_'));
    
    else if(regex_match(childs, ORrgx)) {
        tokens = split(childs, regex("\\(|\\||\\)"));
        
        string last = tokens.back();
        RuleDTD rule = initRule('|', (char) last.at(0));

        for(int i = 0; i < tokens.size() - 1; ++i)
            addRule(rule, (char*) tokens.at(i).c_str());

        addElementRule(element, rule);     
    }

    else if(regex_match(childs, ANDrgx)) {
        tokens = split(childs, regex(","));

        for(auto & token : tokens) {
            
            if(regex_match(token, ORrgx)) {
                tokens = split(token, regex("\\(|\\||\\)"));

                string last = tokens.back();
                RuleDTD rule = initRule('|', (char) last.at(0));

                for(int i = 0; i < tokens.size() - 1; ++i)
                    addRule(rule, (char*) tokens.at(i).c_str());    

                addElementRule(element, rule);
            }
            else {
                token.erase(remove(token.begin(), token.end(), '('), token.end());
                token.erase(remove(token.begin(), token.end(), ')'), token.end());
                
                bool hasRGX = regex_match(token, regex(".+[*|+|?]"));

                RuleDTD rule = initRule(',', hasRGX ? (char) token.back() : '_');

                addRule(rule, (char*) (hasRGX ? token.substr(0, token.size() - 1) : token).c_str());
                addElementRule(element, rule);
            }   
        }
    }
}

static void addElement(DocumentDTD document, ElementDTD element) {
    if(document->elementsUsed == 0)
        document->elementList = (ElementDTD*) malloc(sizeof(ElementDTD));

    else if(document->elementsUsed == document->elementsSize) {
        document->elementsSize *= 2;
        document->elementList = (ElementDTD*) realloc(document->elementList, document->elementsSize * sizeof(ElementDTD));
    }
    
    document->elementList[document->elementsUsed++] = element;
}


static void parseElement(DocumentDTD document, char* elementString) {
    vector<string> tokens = split(string(elementString), regex("(\\s+|\n+|>)"));

    ElementDTD element = initElement((char*) tokens.at(1).c_str());
    parseChilds(element, tokens.at(2));
    addElement(document, element);
}


static void addAttribute(AttributeListDTD attributeList, char* attributeName, char* attributeType, char* attributeOption) {
    if(attributeList->attributesUsed == 0) {
        attributeList->attributeName = (char**) malloc(sizeof(char*));
        attributeList->attributeType = (char**) malloc(sizeof(char*));
        attributeList->attributeOption = (char**) malloc(sizeof(char*));
    }
    else if(attributeList->attributesUsed == attributeList->attributesSize) {
        attributeList->attributesSize *= 2;
        attributeList->attributeName = (char**) realloc(attributeList->attributeName, attributeList->attributesSize * sizeof(char*));
        attributeList->attributeType = (char**) realloc(attributeList->attributeType, attributeList->attributesSize * sizeof(char*));
        attributeList->attributeOption = (char**) realloc(attributeList->attributeOption, attributeList->attributesSize * sizeof(char*));
    }
    
    attributeList->attributeName[attributeList->attributesUsed] = strdup(attributeName);
    attributeList->attributeType[attributeList->attributesUsed] = strdup(attributeType);
    attributeList->attributeOption[attributeList->attributesUsed++] = strdup(attributeOption);
}


static bool notInChilds(RuleDTD* rules, int size, vector<string> childs, string* impostor) {
    bool found;
    
    for(auto & c : childs) {
        found = false;

        for(int i = 0; i < size; ++i)
            for(int j = 0; j < rules[i]->childsUsed; ++j)
                if(strcmp(rules[i]->childs[j], c.c_str()) == 0)
                    found = true;

        if(!found) {
            *impostor = c;
            return true;
        }
    }

    return false;
}


static int childsCount(string child, vector<string> childs) {
    int counter = 0;

    for(auto & c : childs)
        if(child.compare(c) == 0)
            counter++;

    return counter;
}


static void validateElementRules(ElementDTD element, set<string> & errors, vector<string> childs, bool* ok) {

    // 1. verificar se childs.size == 0, para elementos que têm empty EMPTY
    if(element->rules[0]->rule == 'E' && !childs.empty()) {
        errors.insert(string(element->elementName) + string(" can't have child nodes!!"));
        *ok = false;
    }

    string impostor;

    // 2. verificar se existe um filho que não pode estar nos filhos deste elemento
    if(element->rules[0]->rule != 'A' && element->rules[0]->rule != 'E' && notInChilds(element->rules, element->rulesUsed, childs, &impostor)) {
        errors.insert(impostor + string(" should not be on ") + string(element->elementName) + string(" childs!!"));
        *ok = false;
    }

    // 3. 
    // AND: + -> pelo menos um igual
    //      * -> indiferente
    //      _ -> só 1
    //      ? -> 0 ou 1
    // OR:  igual mas para todos os que estão lá

    if(element->rules[0]->rule != 'A' && element->rules[0]->rule != 'E')
        
        for(int i = 0; i < element->rulesUsed; ++i) {
            if(element->rules[i]->rule == ',')
                switch(element->rules[i]->regex) {
                    case '+':
                        for(int j = 0; j < element->rules[i]->childsUsed; ++j)
                            if(childsCount(string(element->rules[i]->childs[j]), childs) == 0) {
                                errors.insert(string(element->elementName) + 
                                            string(" should atleast have one " + string(element->rules[i]->childs[j]) + " node as a child!!"));
                                *ok = false;
                            }
                    
                        break;

                    case '_':
                        for(int j = 0; j < element->rules[i]->childsUsed; ++j)
                            if(childsCount(string(element->rules[i]->childs[j]), childs) != 1 && strcmp(element->rules[i]->childs[j], "#PCDATA") != 0) {
                                errors.insert(string(element->elementName) + 
                                            string(" should have one and only one " + string(element->rules[i]->childs[j]) + " node as a child!!"));
                                *ok = false;
                            }

                        break;

                    case '?':
                        for(int j = 0; j < element->rules[i]->childsUsed; ++j)
                            if(childsCount(string(element->rules[i]->childs[j]), childs) > 2) {
                                errors.insert(string(element->elementName) + 
                                            string(" can't have more than one " + string(element->rules[i]->childs[j]) + " node as a child!!"));
                                *ok = false;
                            }

                        break;
                    
                    default:
                        break;
                }

            else {

            }
        }


}   


static vector<vector<string>> getAttributeList(DocumentDTD document, string elementName) {
    vector<vector<string>> result;
    vector<string> temp;

    for(int i = 0; i < document->attributeListsUsed; i++)
        if(strcmp(document->attributeList[i]->elementName, elementName.c_str()) == 0)
            for(int j = 0; j < document->attributeList[i]->attributesUsed; j++) {
                temp.push_back(string(document->attributeList[i]->attributeName[j]));
                temp.push_back(string(document->attributeList[i]->attributeType[j]));
                temp.push_back(string(document->attributeList[i]->attributeOption[j]));

                result.push_back(temp);
                temp.clear();
            }

    return result;
}


static void parseAttributeList(DocumentDTD document, char* attributeListString) {
    vector<string> tokens = split(string(attributeListString), regex("(\\s+|\n+|>)"));

    AttributeListDTD attributeList = initAttributeList();

    for(int i = 1; i < tokens.size();) {
        if(i == 1)
            attributeList->elementName = strdup((char*) tokens.at(i++).c_str());
        else {
            addAttribute(attributeList, (char*) tokens.at(i).c_str(), 
                                        (char*) tokens.at(i + 1).c_str(),
                                        (char*) regex_replace(tokens.at(i + 2), regex("\""), string("")).c_str());
            i += 3;
        }
    }

    if(document->attributeListsUsed == 0)
        document->attributeList = (AttributeListDTD*) malloc(sizeof(struct attributeList));

    else if(document->attributeListsUsed == document->attributeListsSize) {
        document->attributeListsSize *= 2;
        document->attributeList = (AttributeListDTD*) realloc(document->attributeList, document->attributeListsSize * sizeof(struct attributeList));
    }

    document->attributeList[document->attributeListsUsed++] = attributeList;
}


static void printAttributeList(DocumentDTD document) {
    for(int i = 0; i < document->attributeListsUsed; i++) {
        for(int j = 0; j < document->attributeList[i]->attributesUsed; j++)
            cout << document->attributeList[i]->attributeName[j] << endl 
                 << document->attributeList[i]->attributeType[j] << endl 
                 << document->attributeList[i]->attributeOption[j] << endl << endl;

        cout << endl;
    }
        
}


static void parseDTD(DocumentDTD document) {
    char* temp = strdup(document->source);
    char* start = &(temp[1]);
    char sep = ' ';
    int i = 0, seek = 0;

    for(char* iter = start; *iter != '\0'; ++iter, ++seek)
        if(*iter == sep) {
            *iter = '\0';

            if(i > 0)
                break;

            start = iter + 1;
            i++;
        }

    document->root = strdup(start);
    temp = strdup(document->source);
    start = &(temp[0]);

    sep = '<';

    for(char* iter = start, i = 0; *iter != '\0'; ++iter)
        if(*iter == sep) {
            *iter = '\0';

            if(i > 0) {
                if(regex_search(string(start), regex("!ELEMENT")))
                    parseElement(document, start);

                if(regex_search(string(start), regex("!ATTLIST")))
                    parseAttributeList(document, start);
            }

            i++;
            start = iter + 1;
        }

    if(regex_search(string(start), regex("!ELEMENT")))
        parseElement(document, start);

    if(regex_search(string(start), regex("!ATTLIST")))
        parseAttributeList(document, start);
}


DocumentDTD initDocument() {
    DocumentDTD document = (DocumentDTD) malloc(sizeof(struct document));
    
    document->elementsUsed = 0, document->elementsSize = 1;
    document->attributeListsUsed = 0; document->attributeListsSize = 1;
    
    return document;
}


void parseDocument(DocumentDTD document, string path) {
    size_t size;
    FILE* fp = fopen(path.c_str(), "r");

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* buffer = (char*) malloc((size + 1) * sizeof(char));
    fread(buffer, sizeof(char), size, fp);
    buffer[size] = '\0';
    fclose(fp);

    document->source = strdup(buffer);
    free(buffer);

    parseDTD(document);
}


static bool inVector(vector<string> vec, string elem) {
    for(int i = 0; i < vec.size(); i++)
        if(vec.at(i).compare(elem) == 0)
            return true;

    return false;
}


static bool inVectorByName(vector<string> names, string name, vector<string> values, string value) {
    for(int i = 0; i < names.size(); i++)
        if(names.at(i).compare(name) == 0)
            return (values.at(i).compare(value) == 0);

    return false;
}


void validateAttributes(DocumentDTD document, set<string> & errors, string elementName, vector<string> attributeName, vector<string> attributeValue, bool* ok) {
    vector<vector<string>> temp = getAttributeList(document, elementName);

    for(int i = 0; i < temp.size(); i++) {
        if(temp.at(i).at(2).compare("#REQUIRED") == 0 && !inVector(attributeName, temp.at(i).at(0))) {
            errors.insert(temp.at(i).at(0) + string(" its a Required Attribute"));
            *ok = false;
        }

        if(regex_match(temp.at(i).at(1), regex("\\((.+|)+\\)")) && 
           !inVectorByName(attributeName, temp.at(i).at(0), attributeValue, temp.at(i).at(2)) &&
           inVector(attributeName, temp.at(i).at(0)))

            errors.insert(temp.at(i).at(0) + string(" should have ") + temp.at(i).at(2) + string(" as a Value"));
            *ok = false;
    }
}


void validateElements(DocumentDTD document, set<string> & errors, string elementName, vector<string> childs, bool* ok) {
    for(int i = 0; i < document->elementsUsed; ++i)
        if(strcmp(document->elementList[i]->elementName, elementName.c_str()) == 0)
            validateElementRules(document->elementList[i], errors, childs, ok);
}


static void deleteRule(RuleDTD rule) {
    if(rule->childsUsed > 0) {
        for(int i = 0; i < rule->childsUsed; i++)
            free(rule->childs[i]);

        free(rule->childs);
    }

    free(rule);
}


static void deleteElement(ElementDTD element) {
    free(element->elementName);
    
    if(element->rulesUsed > 0) {
        for(int i = 0; i < element->rulesUsed; i++)
            deleteRule(element->rules[i]);
    
        free(element->rules);
    }

    free(element);
}


static void deleteAttributeList(AttributeListDTD attributeList) {
    free(attributeList->elementName);

    if(attributeList->attributesUsed > 0) {
        for(int i = 0; i < attributeList->attributesUsed; ++i) {
            free(attributeList->attributeName[i]);
            free(attributeList->attributeOption[i]);
            free(attributeList->attributeType[i]);
        }

        free(attributeList->attributeName);
        free(attributeList->attributeOption);
        free(attributeList->attributeType);
    }
    
    free(attributeList);
}


void deleteDocument(DocumentDTD document) {
    if(document->attributeListsUsed > 0 || document->elementsUsed > 0) {
        free(document->source);
        free(document->root);
    }

    if(document->elementsUsed > 0) {
        for(int i = 0; i < document->elementsUsed; ++i)
            deleteElement(document->elementList[i]);

        free(document->elementList);
        document->elementList = NULL;
        document->elementsUsed = 0;
        document->elementsSize = 1;
    }

    if(document->attributeListsUsed > 0) {
        for(int i = 0; i < document->attributeListsUsed; ++i)
            deleteAttributeList(document->attributeList[i]);

        free(document->attributeList);
        document->attributeList = NULL;
        document->attributeListsUsed = 0;
        document->attributeListsSize = 1;
    }


}