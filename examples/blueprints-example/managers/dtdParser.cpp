#include "dtdParser.h"

struct document {
    char* source;
    char* root;
    ElementDTD* elementList;
    int elementsUsed, elementsSize;
    int attributeListsUsed, attributeListsSize;
    AttributeListDTD* attributeList;
};


struct element {
    char* tagName;
    char* ruleType;
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


static ElementDTD initElement() {
    ElementDTD element = (ElementDTD) malloc(sizeof(struct element));
    return element;
}


static void parseElement(DocumentDTD document, char* elementString) {
    char* token = strtok(elementString, " ");
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
    int used = 0, size = 1;
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

    vector<string> errors;
    vector<string> names;
    vector<string> values;

    /*names.push_back("CATEGORY");
    values.push_back("Table");

    validateAttributes(document, errors, string("PRODUCT"), names, values);

    for(int i = 0; i < errors.size(); i++)
        cout << errors.at(i) << endl;*/
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


void validateAttributes(DocumentDTD document, vector<string> & errors, string elementName, vector<string> attributeName, vector<string> attributeValue) {
    vector<vector<string>> temp = getAttributeList(document, elementName);

    for(int i = 0; i < temp.size(); i++) {
        if(temp.at(i).at(2).compare("#REQUIRED") == 0 && !inVector(attributeName, temp.at(i).at(0))) {
            errors.push_back(temp.at(i).at(0) + string(" its a Required Attribute"));
        }

        if(regex_match(temp.at(i).at(1), regex("\\((.+|)+\\)")) && 
           !inVectorByName(attributeName, temp.at(i).at(0), attributeValue, temp.at(i).at(2)) &&
           inVector(attributeName, temp.at(i).at(0)))

            errors.push_back(temp.at(i).at(0) + string(" should have ") + temp.at(i).at(2) + string(" as a Value"));
    }
}
