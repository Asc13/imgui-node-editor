#include <application.h>

#include "utilities/imfilebrowser.h"
#include "utilities/builders.h"
#include "utilities/widgets.h"


#include <imgui.h>
#include <imgui_node_editor.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>


#include <string>
#include <vector>
#include <regex>
#include <queue>
#include <tuple>
#include <map>
#include <algorithm>
#include <utility>
#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <dlfcn.h>
#include "managers/rapidxml.hpp"
#include "managers/rapidjson/document.h"
#include "managers/rapidjson/writer.h"
#include "managers/rapidjson/stringbuffer.h"
#include "managers/dtdParser.h"
#include "managers/configParser.h"
#include "managers/tinyxml2.cpp"

using namespace std;
using namespace tinyxml2;
using namespace rapidjson;


string path = "examples/blueprints-example/";
mutex mtx;

static inline ImRect ImGui_GetItemRect() {
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}


static inline ImRect ImRect_Expanded(const ImRect& rect, float x, float y) {
    auto result = rect;
    result.Min.x -= x;
    result.Min.y -= y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}


namespace ed = ax::NodeEditor;
namespace util = ax::NodeEditor::Utilities;

using namespace ax;
using ax::Widgets::IconType;

typedef void* (*func_handle)(...);

static ed::EditorContext*        m_Editor = nullptr;
vector<tuple<int, float, float>> json_info;


struct Graph;

vector<Graph*>       secondaryGraphs;


bool                                xml_ready = false;
bool                                xml_changed = false;
bool                                json_ready = false;
bool                                dtd_ready = false;
bool                                config_ready = false;
bool                                runnable = false;
bool                                valid = true;

bool                                oneOpen = false;
int                                 indexOpen = -1;

char                                xml_name[500] =  "";
char                                temp_name[500] = "";
string                              json_name;
string                              dtd_name;
string                              config_name;

char                                value[200] = "";
char                                att[200] = "";
vector<tuple<string, string>>       attributes;
int                                 attributes_used = 0;

int                                 m_NextId = 1;
int                                 current = 0;
bool                                isClear = true;
bool                                alert = false;


enum class PinType {
    Flow,
    Bool,
    Int,
    Float,
    Double,
    String,
    Object,
    Function,
    Delegate
};


enum class PinKind {
    Output,
    Input
};


enum class NodeType {
    Element
};


struct Node;


struct Pin {
    ed::PinId   ID;
    ::Node*     Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, string name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input) {}
};


struct Node {
    ed::NodeId ID;
    std::string Name;
    bool HasValue;
    bool Closed;
    bool Root;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    Node(int id, string name, bool hasValue, bool closed, bool root, NodeType type, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), HasValue(hasValue), Closed(closed), Root(root), Color(color), Type(type), Size(0, 0) {}
};


struct Link {
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255) {}
};


struct NodeIdLess {
    bool operator()(const ed::NodeId& lhs, const ed::NodeId& rhs) const {
        return lhs.AsPointer() < rhs.AsPointer();
    }
};


static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f) {
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);

    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}


struct Graph {
    int current;
    int level_x;
    int level_y;
    ed::NodeId ID;
    Node* node;
    Graph* parent;
    vector<Graph*> childs;
};


struct Example:

    public Application {
    
        using Application::Application;

        int GetNextId() {
            return m_NextId++;
        }


        ed::LinkId GetNextLinkId() {
            return ed::LinkId(GetNextId());
        }


        void TouchNode(ed::NodeId id) {
            m_NodeTouchTime[id] = m_TouchTime;
        }


        float GetTouchProgress(ed::NodeId id) {
            auto it = m_NodeTouchTime.find(id);

            if(it != m_NodeTouchTime.end() && it->second > 0.0f)
                return (m_TouchTime - it->second) / m_TouchTime;
            else
                return 0.0f;
        }


        void UpdateTouch() {
            const auto deltaTime = ImGui::GetIO().DeltaTime;
            
            for(auto& entry : m_NodeTouchTime) {
                if(entry.second > 0.0f)
                    entry.second -= deltaTime;
            }
        }


        void FindNode(Graph* graph, Node** node, ed::NodeId id, bool* found) {
            if(graph) {
                if(graph->node->ID == id) {
                    *node = graph->node;
                    *found = true;
                }

                if(!(*found))
                    for(auto & c : graph->childs)
                        FindNode(c, node, id, found);
            }
        }


        bool isLinkedByPins(Pin* a, Pin* b) {
            for(auto & link : m_Links)
                if(link.StartPinID == a->ID && link.EndPinID == b->ID)
                    return true;
            
            for(auto & link : m_AttributeLinks)
                if(link.StartPinID == a->ID && link.EndPinID == b->ID)
                    return true;

            return false;
        }


        Link* FindLink(ed::LinkId id, bool* isAttribute) {
            for(auto& link : m_Links)
                if(link.ID == id)
                    return &link;

            for(auto& link : m_AttributeLinks)
                if(link.ID == id) {
                    *isAttribute = true;
                    return &link;
                }

            return nullptr;
        }


        void FindPin(Graph* graph, ed::PinId id, Pin** pin, bool* found) {
            if(!id)
                *pin = nullptr;
            
            if(graph) {
                for(auto& p : graph->node->Inputs)
                    if(p.ID == id) {
                        *pin = &p;
                        *found = true;
                    }

                if(!(*found))
                    for(auto& p : graph->node->Outputs)
                        if(p.ID == id) {
                            *pin = &p;
                            *found = true;
                        }

                if(!(*found))
                    for(auto & c : graph->childs)
                        FindPin(c, id, pin, found);
            }
        }


        void FindNodeWithInputPin(Graph* graph, Node** node, ed::PinId id, bool* found) {
            if(!id)
                *node = nullptr;

            if(graph) {
                for(auto& pin : graph->node->Inputs)
                    if(pin.ID == id)
                        *node = graph->node;

                if(!(*found))
                    for(auto & c : graph->childs)
                        FindNodeWithInputPin(c, node, id, found);
            }
        }


        void FindNodeWithOutputPin(Graph* graph, Node** node, ed::PinId id, bool* found) {
            if(!id)
                *node = nullptr;

            if(graph) {
                for(auto& pin : graph->node->Outputs)
                    if(pin.ID == id) {
                        *node = graph->node;
                        *found = true;
                    }

                if(!(*found))
                    for(auto & c : graph->childs)
                        FindNodeWithOutputPin(c, node, id, found);
            }
        }


        void FindPinByAttribute(Graph* graph, string element, string attribute, Pin** pin, bool IO, bool* found) {
            if(graph) {
                if(element.compare(graph->node->Name) == 0) {

                    for(auto& p : (IO ? graph->node->Inputs : graph->node->Outputs))
                        if(regex_search(p.Name, regex(attribute))) {
                            *pin = &p;
                            *found = true;
                        }
                }

                if(!(*found))
                    for(auto & c : graph->childs)
                        FindPinByAttribute(c, element, attribute, pin, IO, found);

            }
        }


        void DeleteLinkToPin(ed::PinId id) {
            int i = 0;

            for(auto& link : m_Links) {
                if(link.EndPinID == id)
                    m_Links.erase(m_Links.begin() + i);
                i++;
            }

            i = 0;

            for(auto& link : m_AttributeLinks) {
                if(link.EndPinID == id)
                    m_AttributeLinks.erase(m_AttributeLinks.begin() + i);
                i++;
            }
        }


        bool IsPinLinked(ed::PinId id) {
            if(!id)
                return false;

            for(auto& link : m_Links)
                if(link.StartPinID == id || link.EndPinID == id)
                    return true;

            for(auto& link : m_AttributeLinks)
                if(link.StartPinID == id || link.EndPinID == id)
                    return true;

            return false;
        }


        bool CanCreateLink(Pin* a, Pin* b) {
            if(!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
                return false;

            return true;
        }


        bool CanCreateAttributeLink(Pin* a, Pin* b) {
            string name1 = split(a->Name, regex(" = ")).at(0),
                   name2 = split(b->Name, regex(" = ")).at(0);

            return isValidLink(configs, name1, name2);
        }


        void BuildNode(Node* node) {
            for(auto& input : node->Inputs) {
                input.Node = node;
                input.Kind = PinKind::Input;
            }

            for(auto& output : node->Outputs) {
                output.Node = node;
                output.Kind = PinKind::Output;
            }
        }


        PinType typeMap(string type) {
            if(type.compare("Bool") == 0)
                return PinType::Bool;
            
            if(type.compare("Int") == 0)
                return PinType::Int;

            if(type.compare("Float") == 0)
                return PinType::Float;
            
            if(type.compare("Double") == 0)
                return PinType::Double;
            
            if(type.compare("String") == 0)
                return PinType::String;

            else
                return PinType::Object;
        }


        string asString(PinType type) {
            if(type == PinType::Bool)
                return string("Bool");
            
            if(type == PinType::Int)
                return string("Int");

            if(type == PinType::Float)
                return string("Float");
            
            if(type == PinType::Double)
                return string("Double");
            
            if(type == PinType::String)
                return string("String");

            else
                return string("Object");
        }


        tuple<string, string, bool> searchAttribute(string name, vector<tuple<string, string, bool>> map) {
            tuple<string, string, bool> result;

            for(int i = 0; i < map.size(); i++)
                if(name.compare(get<0>(map.at(i))) == 0)
                    return map.at(i);

            return result;
        }


        Node* SpawnElementNode(bool isRoot, string name, vector<tuple<string, string, bool>> map, 
                               vector<tuple<string, string>> attributes, bool hasValue, bool closed) {

            Node* n = new Node(GetNextId(), name, hasValue, closed, isRoot, NodeType::Element);

            tuple<string, string, bool> temp;
            
            if(!isRoot)
                n->Inputs.emplace_back(GetNextId(), "", PinType::Flow);
            
            n->Outputs.emplace_back(GetNextId(), "", PinType::Flow);
            
            if(!map.empty())
                for(auto s : attributes) {
                    temp = searchAttribute(get<0>(s), map);

                    if(get<2>(temp))
                        n->Inputs.emplace_back(GetNextId(), get<0>(s) + " = " + get<1>(s), typeMap(get<1>(temp)));
                    
                    else {
                        n->Outputs.emplace_back(GetNextId(), get<0>(s) + " = " + get<1>(s), typeMap(get<1>(temp)));
                    }
                }
            else
                for(auto s : attributes)
                    n->Inputs.emplace_back(GetNextId(), get<0>(s) + " = " + get<1>(s), PinType::String);
            
            return n;
        }


        int levelYOrder(Graph* node, vector<vector<int>>* levels_x) {
            for(auto l : *levels_x)
                for(int i = 0; i < l.size(); ++i)
                    if(l.at(i) == node->current)
                        return i;

            return 0;   
        }


        vector<vector<int>> levelXOrder(Graph* node) {
            vector<vector<int>> levels;
            queue<Graph*> main;
            
            main.push(node);

            vector<int> temp;

            while(!main.empty()) {
                int n = main.size();

                for(int i = 0; i < n; ++i) {
                    Graph* c = main.front();
                    main.pop();
                    temp.push_back(c->current);

                    for(auto child : c->childs)
                        main.push(child);
                }

                levels.push_back(temp);
                temp.clear();
            }

            return levels;
        }


        int height(Graph* node) {
            vector<int> heights;
            heights.push_back(0);
            
            if(!node) 
                return 0;
            
            else {
                for(auto n : node->childs) {
                    heights.push_back(height(n));
                }
                
                return *max_element(heights.begin(), heights.end()) + 1;
            }
        }


        int width(vector<vector<int>>* levels_x, Graph* node) {
            return ((*levels_x).at(node->level_x)).size();
        }


        int maxWidth(vector<vector<int>>* levels_x) {
            int max = 0;

            for(int i = 0; i < (*levels_x).size(); ++i)
                if(((*levels_x).at(i)).size() > max)
                    max = ((*levels_x).at(i)).size();

            return max; 
        }


        void readjust(Graph* graph, int max_witdh, int max_heigth, vector<vector<int>>* levels_x) {
            if(graph) {
                bool isRoot = (graph->parent) ? false : true;
                
                float tam_max_y = (300.0 * (float) max_witdh);
                float x, y;
                
                
                if(isRoot) {
                    x = 0.0; y = tam_max_y / 2.0;
                }
                else {
                    x = 400.0 * ((float) graph->level_x); 
                    y = (tam_max_y / (width(levels_x, graph) + 1.0)) * ((float) graph->level_y + 1.0);
                }

                ed::SetNodePosition(graph->ID, ImVec2(x, y));

                for(auto n : graph->childs)
                    readjust(n, max_witdh, max_heigth, levels_x);
            }
        }


        void lookup(Graph* graph, Graph** res, ed::NodeId ID, bool* found) {
            if(graph) {
                if(!(graph->childs).empty())
                    lookup((graph->childs).at(0), res, ID, found);

                if(graph->ID == ID) {
                    *res = graph;
                    *found = true;
                }

                if(!(*found))
                    lookup(rightSibling(graph), res, ID, found);
            }
        }


        bool toBool(string value) {
            if(value.compare("True") == 0 || 
               value.compare("1") == 0 || 
               value.compare("true") == 0)
                return true;
            return false;
        }


        void* cast(string att, PinType type) {
            vector<string> tokens = split(att, regex(" = "));

            if(type == PinType::Bool)
                return reinterpret_cast<void*>(toBool(tokens[1]));

            if(type == PinType::Int)
                return reinterpret_cast<void*>(stoi(tokens[1]));

            /*if(type == PinType::Float)
                return reinterpret_cast<void*>(static_cast<float*>(stof(tokens[1])));

            if(type == PinType::Double)
                return reinterpret_cast<void*>(&stod(tokens[1]));*/

            if(type == PinType::String) {
                char* value = strdup(tokens[1].c_str());
                return value;
            }

            return NULL;
        }


        string uncast(void* value, PinType type) {
            if(type == PinType::Bool)
                return ((intptr_t) value) ? string("True") : string("False");

            if(type == PinType::Int)
                return to_string((intptr_t) value);

            /*if(type == PinType::Float)
                return reinterpret_cast<void*>(static_cast<float*>(stof(tokens[1])));

            if(type == PinType::Double)
                return reinterpret_cast<void*>(&stod(tokens[1]));*/

            if(type == PinType::String)
                return string((char*) value);

            return string("");
        }


        void NextNodeByLink(Graph* graph, Graph** next, string pointer, bool* found) {
            if(graph) {
                if(graph->node->Name.compare(pointer) == 0) {
                    *next = graph;
                    *found = true;
                }

                if(!(*found))
                    for(auto & c : graph->childs)
                        NextNodeByLink(c, next, pointer, found);
            }
        }


        void runGraph(Graph* g, set<int> & used) {
            if(g) {
                Graph* next = NULL;
                bool found = false;

                string pointer = getPointer(configs, g->node->Name, used);

                NextNodeByLink(graph, &next, pointer, &found);

                if(found)
                    runGraph(next, used);

                if(g->node->HasValue) {
                    string file = g->node->Inputs.back().Name;
                    string object = regex_replace(file, regex("cpp"), string("o"));
                    string function = regex_replace(file, regex(".cpp"), string(""));

                    system(string("cd /home/bruno/Desktop/---/PI/imgui-node-editor/examples/blueprints-example/files/functions/ ;"
                           "rm " + object + "; "
                           "g++ -shared " + file + " -o " + object).c_str());
                    
                    void* o_file = dlopen(string("examples/blueprints-example/files/functions/" + object).c_str(), RTLD_LAZY);

                    func_handle handler;
                    int i1 = 1 - (g->parent ? 0 : 1), i2 = i1 + 1;
                    int o3 = 1 - g->node->HasValue;

                    void* z;
                    void* x = cast(g->node->Inputs[i1].Name, g->node->Inputs[i1].Type); 
                    void* y = cast(g->node->Inputs[i2].Name, g->node->Inputs[i2].Type);

                    if(o_file) {
                        auto link = reinterpret_cast<func_handle>(dlsym(o_file, function.c_str()));

                        if(link) {
                            z = link(x, y);
                        }

                        dlclose(o_file);
                    }

                    vector<string> tokens = split(g->node->Outputs[o3].Name, regex(" = "));        
                    g->node->Outputs[o3].Name = tokens.at(0) + " = " + uncast(z, g->node->Outputs[o3].Type);
                    
                    tuple<string, string> elementAtt = getConfigByIndex(configs, g->node->Name, *used.begin());
                    used.erase(used.begin());

                    found = false;
                    NextNodeByLink(graph, &next, get<0>(elementAtt), &found);

                    if(found)
                        for(int i = (next->parent ? 1 : 0); i < (next->node->Inputs.size() - next->node->HasValue); i++) {
                            tokens = split(next->node->Inputs[i].Name, regex(" = "));

                            if(tokens.at(0).compare(get<1>(elementAtt)) == 0)       
                                next->node->Inputs[i].Name = tokens.at(0) + " = " + uncast(z, g->node->Outputs[o3].Type);
                        }
                } 
            }
        }
        

        Graph* getGraphByNode(ed::NodeId ID, bool* isPrimary, int* index) {
            Graph* temp = NULL;
            bool found = false;

            lookup(graph, &temp, ID, &found);

            if(found) {
                *isPrimary = true;
                return temp;
            }

            for(auto g : secondaryGraphs) {
                lookup(g, &temp, ID, &found);

                if(found)
                    return temp;
                
                (*index)++;
            }

            return temp;
        }


        void linkGraphs(ed::PinId startPinId, ed::PinId endPinId, PinType type) {
            Node *nStart, *nEnd;
            bool foundS = false, foundE = false;

            FindNodeWithOutputPin(graph, &nStart, startPinId, &foundS);

            if(!foundS)
                for(auto & sG : secondaryGraphs)
                    FindNodeWithOutputPin(sG, &nStart, startPinId, &foundS);

            FindNodeWithInputPin(graph, &nEnd, endPinId, &foundE);

            if(!foundE)
                for(auto & sG : secondaryGraphs)
                    FindNodeWithInputPin(sG, &nEnd, endPinId, &foundE);

            bool isPrimaryStart, isPrimaryEnd = false;
            int indexStart = 0, indexEnd = 0;

            Graph *gStart = getGraphByNode(nStart->ID, &isPrimaryStart, &indexStart);
            Graph *gEnd = getGraphByNode(nEnd->ID, &isPrimaryEnd, &indexEnd);

            if(!isPrimaryEnd && (gStart != gEnd)) {
                gEnd->parent = gStart;
                (gStart->childs).push_back(gEnd);

                m_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                m_Links.back().Color = GetIconColor(type);

                secondaryGraphs.at(indexEnd) = NULL;
                secondaryGraphs.erase(secondaryGraphs.begin() + indexEnd);
            }
        }


        void createGraph(Node* node) {
            Graph* temp = new Graph;
            vector<Graph*> childs;

            temp->node = node;
            temp->parent = NULL;
            temp->ID = node->ID;
            temp->level_x = 0;
            temp->level_y = 0;
            temp->childs = childs;
            temp->current = current++;

            if(isClear) {
                graph = temp;          
                isClear = false;
            }
            else
                secondaryGraphs.push_back(temp);
        }


        void recDelete(Graph* graph) {
            if(graph) {
                for(auto c : graph->childs)        
                    recDelete(c);

                delete graph->node;
                graph->childs.clear();
                
                if(graph->parent)
                    delete graph;
            }
        }


        void deleteGraph(Graph** graph) {
            mtx.lock();
            recDelete(*graph);
            *graph = NULL;
            mtx.unlock();
        }


        Graph* rightSibling(Graph* graph) {
            if(graph && graph->parent) {
                int i = 0;

                for(auto c : (graph->parent)->childs) {
                    i++;

                    if(c->ID == graph->ID && i < ((graph->parent)->childs).size())
                        return ((graph->parent)->childs).at(i);
                }
            }

            return NULL;
        }


        void lookupDeleteGraph(Graph* graph, ed::NodeId ID, bool *deleted) {
            if(graph) {
                if(!(graph->childs).empty())
                    lookupDeleteGraph((graph->childs).at(0), ID, deleted);

                if(graph->node->ID == ID) {
                    if(graph->parent) {
                        int i = 0;

                        for(auto a : (graph->parent)->childs) {
                            if(a->ID == graph->node->ID)
                                break;
                            i++;
                        }

                        ((graph->parent)->childs).at(i) = NULL;
                        ((graph->parent)->childs).erase(((graph->parent)->childs).begin() + i);
                    }

                    for(auto i : graph->node->Inputs)
                        DeleteLinkToPin(i.ID);

                    delete graph->node;
                    graph->node = NULL;
                    deleteGraph(&graph);

                    *deleted = true;
                }

                if(!(*deleted) && graph->parent)
                    lookupDeleteGraph(rightSibling(graph), ID, deleted);
            }
        }


        void lookupDeleteSecondaryGraph(ed::NodeId ID, bool *deleted) {
            int i = 0;

            for(auto g : secondaryGraphs) {
                lookupDeleteGraph(g, ID, deleted);

                if(*deleted) {
                    secondaryGraphs.at(i) = NULL;
                    secondaryGraphs.erase(secondaryGraphs.begin() + i);
                    break;
                }

                i++;
            }
        }


        void release() {
            isClear = true;
            current = 0;
            m_NextId = 1;
            runnable = false;
            strcpy(temp_name, "");
            m_Links.clear();
            m_AttributeLinks.clear();
            levels_x.clear();
            errors.clear();

            for(auto g : secondaryGraphs)
                deleteGraph(&g);

            secondaryGraphs.clear();

            if(graph)
                deleteGraph(&graph);

            if(&doc) {
                doc.Clear();
                root_node = NULL;
            }

            deleteConfigs(configs);
            deleteDocument(document);

            dtd_ready = false;
            config_ready = false;
        }


        void printNode(Graph* graph) {
            if(graph) {        
                cout << "ID: " << graph->ID.Get() << endl;
                cout << "Current: " << graph->current << endl;
                cout << "Parent: " << ((graph->parent) ? (int) ((graph->parent)->ID.Get()) : -1) << endl;
                cout << "Level X: " << graph->level_x << endl;
                cout << "Level Y: " << graph->level_y << endl;
                
                cout << "Childs: ";
                
                for(auto c : graph->childs)
                    cout << c->ID.Get() << " ";
                
                cout << endl << endl;

                for(auto c : graph->childs)
                    printNode(c);
            }
        }


        void recalibrateRec(Graph* graph) {
            if(graph) {
                
                graph->current = current;

                for(auto c : graph->childs) {
                    current++;
                    recalibrateRec(c);
                }
            }
        }


        void recalibrate() {
            if(graph) {
                current = 0;
                recalibrateRec(graph);
                levels_x.clear();
                levels_x = levelXOrder(graph);
                link(graph, &levels_x, false);
                readjust(graph, maxWidth(&levels_x), height(graph), &levels_x);
            }
        }

        void revalidate() {
            if(graph) {
                valid = true;
                errors.clear();
                validateGraph(graph);
            }
        }


        void linkAttributes(Graph* graph) {
            vector<vector<string>> configLinks = getLinks(configs);

            Pin* startPin;
            Pin* endPin;
            bool foundS, foundE;

            for(auto& t : configLinks) {
                foundS = false, foundE = false;
                FindPinByAttribute(graph, t.at(0), t.at(1), &startPin, false, &foundS);
                FindPinByAttribute(graph, t.at(2), t.at(3), &endPin, true, &foundE);

                if(foundS && foundE) {
                    m_AttributeLinks.emplace_back(Link(GetNextId(), startPin->ID, endPin->ID));
                    m_AttributeLinks.back().Color = GetIconColor(typeMap(t.at(4)));
                }
            }
        }


        void link(Graph* graph, vector<vector<int>> *levels_x, bool flag) {
            int current = graph->current;
            int parent = (graph->parent) ? (graph->parent)->current : -1;
            
            for(int i = 0; i < (*levels_x).size(); ++i) {
                for(auto l : (*levels_x).at(i))
                    if(l == current) {
                        graph->level_x = i;
                        break;
                    }
            }

            graph->level_y = levelYOrder(graph, levels_x);

            if(flag && graph->parent) {
                m_Links.emplace_back(Link(GetNextLinkId(), graph->parent->node->Outputs[0].ID, graph->node->Inputs[0].ID));
                m_Links.back().Color = GetIconColor(PinType::Flow);
            }

            for(auto c : graph->childs)
                link(c, levels_x, flag);
        }


        void getPositionFromJSON(int ID, float *x, float *y) {
            for(auto n : json_info)
                if(get<0>(n) == ID) {
                    *x = get<1>(n); *y = get<2>(n);
                    break;
                }
        }


        void validateGraph(Graph* graph) {
            vector<string> names;
            vector<string> values;
            vector<tuple<string, string>> nameValue;
            vector<string> tokens;
            vector<string> childs;

            if(graph) {
                for(int i = 1; i < graph->node->Inputs.size() - (graph->node->HasValue ? 1 : 0); ++i) {
                    tokens = split(graph->node->Inputs.at(i).Name, regex(" = "));
                    
                    if(tokens.size() == 1)
                        tokens.push_back("");

                    names.push_back(tokens.at(0));
                    values.push_back(tokens.at(1));
                    nameValue.push_back(make_tuple(tokens.at(0), tokens.at(1)));
                }

                for(auto & c : graph->childs)
                    childs.push_back(c->node->Name);

                if(dtd_ready) {
                    validateAttributes(document, errors, graph->node->Name, names, values, &valid);
                    validateElements(document, errors, graph->node->Name, childs, &valid);
                }
                
                if(config_ready)
                    validateAttributesByElement(configs, graph->node->Name, nameValue, errors, &valid);

                for(auto & c: graph->childs)
                    validateGraph(c);
            }

        }


        void inventoryDisplay(Graph* graph, vector<ed::NodeId> selectedNodes, ImGuiIO io) {
            if(graph) {
                ImGui::PushID(graph->ID.AsPointer());
                auto start = ImGui::GetCursorScreenPos();

                if(const auto progress = GetTouchProgress(graph->ID)) {
                    ImGui::GetWindowDrawList()->AddLine(
                        start + ImVec2(-8, 0),
                        start + ImVec2(-8, ImGui::GetTextLineHeight()),
                        IM_COL32(255, 0, 0, 255 - (int)(255 * progress)), 4.0f);
                }

                bool isSelected = std::find(selectedNodes.begin(), selectedNodes.end(), graph->ID) != selectedNodes.end();
                string temp = "";

                if(!graph->node->Inputs.empty()) {
                    if(graph->node->HasValue)
                        temp = " - " + graph->node->Inputs.back().Name;
                }

                if(ImGui::Selectable((graph->node->Name + temp + "##" + std::to_string(reinterpret_cast<uintptr_t>(graph->ID.AsPointer()))).c_str(), &isSelected)) {
                    
                    if(io.KeyCtrl) {
                        
                        if(isSelected)
                            ed::SelectNode(graph->ID, true);
                        else
                            ed::DeselectNode(graph->ID);
                    }
                    else
                        ed::SelectNode(graph->ID, false);

                    ed::NavigateToSelection();
                }

                ImGui::PopID();

                for(auto & c : graph->childs)
                    inventoryDisplay(c, selectedNodes, io);
            }
        }


        void nodeDisplay(Graph* graph, util::BlueprintNodeBuilder builder, Pin* newLinkPin) {
            if(graph) {

                builder.Begin(graph->node->ID);

                builder.Header(graph->node->Color);
                ImGui::Spring(0);
                ImGui::TextUnformatted(graph->node->Name.c_str());
                ImGui::Spring(1);
                ImGui::Dummy(ImVec2(0, 28));
                ImGui::Spring(0);
                builder.EndHeader();


                for(auto& input : graph->node->Inputs) {
                    auto alpha = ImGui::GetStyle().Alpha;

                    if(newLinkPin && !CanCreateLink(newLinkPin, &input) && &input != newLinkPin)
                        alpha = alpha * (48.0f / 255.0f);

                    builder.Input(input.ID);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                    DrawPinIcon(input, IsPinLinked(input.ID), (int)(alpha * 255));
                    ImGui::Spring(0);

                    if(!input.Name.empty()) {
                        ImGui::TextUnformatted(input.Name.c_str());
                        ImGui::Spring(0);
                    }

                    ImGui::PopStyleVar();
                    builder.EndInput();
                }

                for (auto& output : graph->node->Outputs) {
                    auto alpha = ImGui::GetStyle().Alpha;

                    if(newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                        alpha = alpha * (48.0f / 255.0f);

                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                    builder.Output(output.ID);


                    if (!output.Name.empty()) {
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                    }

                    ImGui::Spring(0);
                    DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
                    ImGui::PopStyleVar();
                    builder.EndOutput();
                }

                builder.End();

                for(auto & c : graph->childs)
                    nodeDisplay(c, builder, newLinkPin);
            }
        }


        void BuildNodes(Graph* graph) {
            if(graph) {
                BuildNode(graph->node);

                for(auto & c : graph->childs)
                    BuildNodes(c);
            }
        }


        void OnStart() override {
            m_Editor = ed::CreateEditor(NULL);
            ed::SetCurrentEditor(m_Editor);
            fileDialog.SetTitle("File Browser");
            fileDialog.Open();
            document = initDocument();
            configs = initConfigs();

            m_HeaderBackground = LoadTexture((path + string("/data/BlueprintBackground.png")).c_str());
            m_SaveIcon         = LoadTexture((path + string("/data/ic_save_white_24dp.png")).c_str());
            m_RestoreIcon      = LoadTexture((path + string("/data/ic_restore_white_24dp.png")).c_str());
        }


        void OnStop() override {
            fileDialog.Close();
            auto releaseTexture = [this](ImTextureID& id) {
                if(id) {
                    DestroyTexture(id);
                    id = nullptr;
                }
            };

            releaseTexture(m_RestoreIcon);
            releaseTexture(m_SaveIcon);
            releaseTexture(m_HeaderBackground);
            
            if(m_Editor) {
                ed::DestroyEditor(m_Editor);
                m_Editor = nullptr;
            }
        }


        ImColor GetIconColor(PinType type) {
            switch(type) {
                default:
                case PinType::Flow:     return ImColor(255, 255, 255);
                case PinType::Bool:     return ImColor(220,  48,  48);
                case PinType::Int:      return ImColor( 68, 201, 156);
                case PinType::Float:    return ImColor(147, 226,  74);
                case PinType::Double:   return ImColor( 82, 127,  43);
                case PinType::String:   return ImColor(124,  21, 153);
                case PinType::Object:   return ImColor( 51, 150, 215);
                case PinType::Function: return ImColor(218,   0, 183);
                case PinType::Delegate: return ImColor(255,  48,  48);
            }
        };


        void DrawPinIcon(const Pin& pin, bool connected, int alpha) {
            IconType iconType;
            ImColor  color = GetIconColor(pin.Type);
            color.Value.w = alpha / 255.0f;
            
            switch(pin.Type) {
                case PinType::Flow:     iconType = IconType::Flow;   break;
                case PinType::Bool:     iconType = IconType::Circle; break;
                case PinType::Int:      iconType = IconType::Circle; break;
                case PinType::Float:    iconType = IconType::Circle; break;
                case PinType::Double:   iconType = IconType::Circle; break;
                case PinType::String:   iconType = IconType::Circle; break;
                case PinType::Object:   iconType = IconType::Circle; break;
                case PinType::Function: iconType = IconType::Circle; break;
                case PinType::Delegate: iconType = IconType::Square; break;
                default:
                    return;
            }

            ax::Widgets::Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
        }


        bool fileExists(string name) {
            ifstream f(name.c_str());
            bool ok = f.good();
            f.close();
            return ok;
        }


        void ShowXmlReader(bool* show = nullptr) {
            if(!(*show))
                return;
                
            fileDialog.Display(&alert);

            if(alert) {
                *show = false;
                alert = false;
                return;
            }

            if(fileDialog.HasSelected()) {
                strcpy(xml_name, fileDialog.GetSelected().string().c_str());
                json_name = regex_replace(string(xml_name), regex("xml"), string("json"));
                dtd_name = regex_replace(string(xml_name), regex("xml"), string("dtd"));
                config_name = regex_replace(string(xml_name), regex("xml"), string("cfg"));
        
                if(strcmp(temp_name, xml_name) == 0)
                    xml_changed = false;

                else {
                    xml_changed = true;
                    strcpy(temp_name, xml_name);
                    release();

                    if(regex_search(xml_name, regex(".xml"))) {
                        if(fileExists(json_name))
                            json_ready = true;

                        else
                            xml_ready = true;

                        if(fileExists(dtd_name))
                            dtd_ready = true;

                        if(fileExists(config_name))
                            config_ready = true;

                        *show = false;
                        fileDialog.ClearSelected();
                    }
                }
            }
        }


        void ShowNodeEditing(bool* show = nullptr, bool* stay = nullptr, Node* node = NULL) {
            if(!ImGui::Begin("Node Editing", show)) {
                ImGui::End();
                return;
            }

            vector<string> tokens;
            auto paneWidth = ImGui::GetContentRegionAvail().x;

            ImGui::BeginHorizontal("Node Editing", ImVec2(paneWidth, 0), 1.0f);
            ImGui::TextUnformatted((string("Edit Node: ") + node->Name).c_str());
            ImGui::EndHorizontal();   
            
            if(node->Inputs.size() > 1) {
                ImGui::Separator();
                ImGui::BeginHorizontal("Inputs", ImVec2(paneWidth, 0), 1.0f);
                ImGui::TextUnformatted("Input Attributes: ");
                ImGui::EndHorizontal();

                for(int i = 1; i < node->Inputs.size() - node->HasValue; ++i) {
                    ImGui::BeginHorizontal("", ImVec2(paneWidth, 0), 1.0f);
                    tokens = split(node->Inputs[i].Name, regex(" = "));

                    if(indexOpen == i) {
                        ImGui::TextUnformatted((asString(node->Inputs[i].Type) + " " + tokens.at(0) + " =").c_str());
                        ImGui::Spacing();
                        ImGui::InputText("", value, IM_ARRAYSIZE(value));

                        if(ImGui::Button("Confirm")) {
                            node->Inputs[i].Name = tokens.at(0) + " = " + string(value);
                            revalidate();
                            strcpy(value, "");
                            oneOpen = false;
                            indexOpen = -1;
                        }
                    }
                    else {
                        ImGui::TextUnformatted((asString(node->Inputs[i].Type) + " " + node->Inputs[i].Name).c_str());

                        if(!oneOpen)
                            if(ImGui::Button((string("Edit ") + tokens.at(0)).c_str())) {
                                oneOpen = true;
                                indexOpen = i;
                            }
                    }

                    ImGui::Spacing();
                    ImGui::EndHorizontal();
                }

                if(node->HasValue) {
                    ImGui::BeginHorizontal("", ImVec2(paneWidth, 0), 1.0f);

                    if(indexOpen == (node->Inputs.size() - 1)) {
                        ImGui::TextUnformatted(string("Node Value:").c_str());
                        ImGui::Spacing();
                        ImGui::InputText("", value, IM_ARRAYSIZE(value));

                        if(ImGui::Button("Confirm")) {
                            node->Inputs.back().Name = string(value);
                            revalidate();
                            strcpy(value, "");
                            oneOpen = false;
                            indexOpen = -1;
                        }
                    }
                    else {
                        ImGui::TextUnformatted((string("Node Value: ") + node->Inputs.back().Name).c_str());
                        
                        if(!oneOpen)
                            if(ImGui::Button(string("Edit Value").c_str())) {
                                oneOpen = true;
                                indexOpen = node->Inputs.size() - 1;
                            }
                    }

                    ImGui::Spacing();
                    ImGui::EndHorizontal();
                }
            }

            if(node->Outputs.size() > (node->Outputs[0].Type == PinType::Flow)) {
                ImGui::Separator();
                ImGui::BeginHorizontal("Outputs", ImVec2(paneWidth, 0), 1.0f);
                ImGui::TextUnformatted("Output Attributes: ");
                ImGui::EndHorizontal();

                for(int i = (node->Outputs[0].Type == PinType::Flow); i < node->Outputs.size(); ++i) {
                    ImGui::BeginHorizontal("", ImVec2(paneWidth, 0), 1.0f);
                    ImGui::TextUnformatted((asString(node->Outputs[i].Type) + " " + node->Outputs[i].Name).c_str());
                    ImGui::Spacing();
                    ImGui::EndHorizontal();
                }

                ImGui::Separator();
            }
            ImGui::End();
        }
        

        void ShowNodeCreation(bool* show = nullptr, bool* stay = nullptr, Node** node = nullptr) {
            if(!ImGui::Begin("Node Creation", show)) {
                ImGui::End();
                return;
            }

            auto paneWidth = ImGui::GetContentRegionAvail().x;

            ImGui::BeginHorizontal("Node Creation", ImVec2(paneWidth, 0), 1.0f);
            ImGui::TextUnformatted("Create Node");
            ImGui::Spring();
            ImGui::EndHorizontal();
            ImGui::Spacing();
            ImGui::BeginHorizontal("Value and Create", ImVec2(paneWidth, 0), 1.0f);
            ImGui::InputText("", value, IM_ARRAYSIZE(value));
            ImGui::Spacing();

            if(ImGui::Button("Create"))
                *stay = false;

            ImGui::EndHorizontal();

            ImGui::Separator();

            for(int i = 0; i < attributes_used; i++) {    
                ImGui::BeginHorizontal(regex_replace(to_string(i), regex("0"), string("Attribute0")).c_str(), ImVec2(paneWidth, 0), 1.0f);
                ImGui::TextUnformatted((get<0>(attributes.at(i)) + " = " + get<1>(attributes.at(i))).c_str());
                ImGui::Spring();
                ImGui::EndHorizontal();
            }

            ImGui::BeginHorizontal("Next Attribute", ImVec2(paneWidth, 0), 1.0f);
            ImGui::InputText("", att, IM_ARRAYSIZE(att));
            ImGui::Spacing();

            vector<string> tempAtt;

            if(ImGui::Button("Add") && regex_search(string(att), regex(".+ = .+"))) {
                tempAtt = split(string(att), regex(" = "));
                attributes.push_back(make_tuple(tempAtt.at(0), tempAtt.at(1)));
                attributes_used++;
                strcpy(att, "");
            }

            ImGui::EndHorizontal();

            vector<tuple<string, string, bool>> map;

            if(!(*stay)) {
                *node = SpawnElementNode(isClear, value, getAttributesTypes(configs, value), attributes, false, false);
                strcpy(value, "");
                strcpy(att, "");
                attributes_used = 0;
                attributes.clear();
                *show = false;
            }
            
            ImGui::End();
        }


        void ShowSave(bool* show = nullptr) {
            if(regex_search(xml_name, regex(".xml"))) {
                ofstream out {json_name.c_str()};
                saveJSON(graph, &out);
                out << "\t]\n}";
                out.close();
                out.clear();

                out.open(xml_name);
                out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl << endl;
                saveXML(graph, &out);
                out.close();
                out.clear();

                *show = false;
            }
        }


        void ShowStyleEditor(bool* show = nullptr) {
            if(!ImGui::Begin("Style", show)) {
                ImGui::End();
                return;
            }

            auto paneWidth = ImGui::GetContentRegionAvail().x;

            auto& editorStyle = ed::GetStyle();
            ImGui::BeginHorizontal("Style buttons", ImVec2(paneWidth, 0), 1.0f);
            ImGui::TextUnformatted("Values");
            ImGui::Spring();
            
            if(ImGui::Button("Reset to defaults"))
                editorStyle = ed::Style();
            
            ImGui::EndHorizontal();
            ImGui::Spacing();
            ImGui::DragFloat4("Node Padding", &editorStyle.NodePadding.x, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Node Rounding", &editorStyle.NodeRounding, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Node Border Width", &editorStyle.NodeBorderWidth, 0.1f, 0.0f, 15.0f);
            ImGui::DragFloat("Hovered Node Border Width", &editorStyle.HoveredNodeBorderWidth, 0.1f, 0.0f, 15.0f);
            ImGui::DragFloat("Selected Node Border Width", &editorStyle.SelectedNodeBorderWidth, 0.1f, 0.0f, 15.0f);
            ImGui::DragFloat("Pin Rounding", &editorStyle.PinRounding, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Pin Border Width", &editorStyle.PinBorderWidth, 0.1f, 0.0f, 15.0f);
            ImGui::DragFloat("Link Strength", &editorStyle.LinkStrength, 1.0f, 0.0f, 500.0f);
            ImGui::DragFloat("Scroll Duration", &editorStyle.ScrollDuration, 0.001f, 0.0f, 2.0f);
            ImGui::DragFloat("Flow Marker Distance", &editorStyle.FlowMarkerDistance, 1.0f, 1.0f, 200.0f);
            ImGui::DragFloat("Flow Speed", &editorStyle.FlowSpeed, 1.0f, 1.0f, 2000.0f);
            ImGui::DragFloat("Flow Duration", &editorStyle.FlowDuration, 0.001f, 0.0f, 5.0f);
            ImGui::DragFloat("Group Rounding", &editorStyle.GroupRounding, 0.1f, 0.0f, 40.0f);
            ImGui::DragFloat("Group Border Width", &editorStyle.GroupBorderWidth, 0.1f, 0.0f, 15.0f);

            ImGui::Separator();

            static ImGuiColorEditFlags edit_mode = ImGuiColorEditFlags_DisplayRGB;
            ImGui::BeginHorizontal("Color Mode", ImVec2(paneWidth, 0), 1.0f);
            ImGui::TextUnformatted("Filter Colors");
            ImGui::Spring();
            ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditFlags_DisplayRGB);
            ImGui::Spring(0);
            ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditFlags_DisplayHSV);
            ImGui::Spring(0);
            ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditFlags_DisplayHex);
            ImGui::EndHorizontal();

            static ImGuiTextFilter filter;
            filter.Draw("", paneWidth);

            ImGui::Spacing();

            ImGui::PushItemWidth(-160);

            for (int i = 0; i < ed::StyleColor_Count; ++i) {
                auto name = ed::GetStyleColorName((ed::StyleColor)i);
                if(!filter.PassFilter(name))
                    continue;

                ImGui::ColorEdit4(name, &editorStyle.Colors[i].x, edit_mode);
            }
            ImGui::PopItemWidth();

            ImGui::End();
        }


        void ShowLeftPane(float paneWidth) {
            auto& io = ImGui::GetIO();

            ImGui::BeginChild("Selection", ImVec2(paneWidth, 0));

            paneWidth = ImGui::GetContentRegionAvail().x;

            static bool showStyleEditor = false, showXMLReader = false, showSave = false;

            ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, 0));
            ImGui::Spring(0.0f, 0.0f);
            
            if(ImGui::Button("Zoom to Content"))
                ed::NavigateToContent();

            ImGui::Spring(0.0f);
            
            if(ImGui::Button(" Save Files ") && !isClear)
                showSave = true;

            ImGui::Spring(0.0f);

            if(ImGui::Button("XMLReader"))
                showXMLReader = true;

            ImGui::EndHorizontal();

            ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, -200));
            ImGui::Spring(0.0f, 0.0f);

            if(ImGui::Button("Show Flow")) {
                for(auto& link : m_Links)
                    ed::Flow(link.ID);
            
                for(auto& link : m_AttributeLinks)
                    ed::Flow(link.ID);
            }

            if(ImGui::Button("Readjust"))
                m_readjust = true;

            if(ImGui::Button("Clear"))
                m_clear = true;

            if(ImGui::Button("Edit Style"))
                showStyleEditor = true;

            ImGui::EndHorizontal();
            ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, -400));
            
            ImGui::Checkbox("Show Ordinals", &m_ShowOrdinals);
            ImGui::Spring(0.0f);
            ImGui::Checkbox("Hide links", &m_hideLinks);
            ImGui::Spring(0.0f);
            
            ImGui::EndHorizontal();
            

            if(showXMLReader) {
                ShowXmlReader(&showXMLReader);
            }

            if(showStyleEditor)
                ShowStyleEditor(&showStyleEditor);

            if(showSave)
                ShowSave(&showSave);


            std::vector<ed::NodeId> selectedNodes;
            std::vector<ed::LinkId> selectedLinks;
            selectedNodes.resize(ed::GetSelectedObjectCount());
            selectedLinks.resize(ed::GetSelectedObjectCount());

            int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
            int linkCount = ed::GetSelectedLinks(selectedLinks.data(), static_cast<int>(selectedLinks.size()));

            selectedNodes.resize(nodeCount);
            selectedLinks.resize(linkCount);

            int saveIconWidth     = GetTextureWidth(m_SaveIcon);
            int saveIconHeight    = GetTextureWidth(m_SaveIcon);
            int restoreIconWidth  = GetTextureWidth(m_RestoreIcon);
            int restoreIconHeight = GetTextureWidth(m_RestoreIcon);

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f
            );

            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextUnformatted("Nodes");
            ImGui::Indent();

            inventoryDisplay(graph, selectedNodes, io);

            ImGui::Unindent();

            static int changeCount = 0;

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f
            );

            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextUnformatted("Graph validation");
            
            if(config_ready) {
                auto start = ImGui::GetCursorScreenPos();

                ImGui::GetWindowDrawList()->AddLine(
                    start + ImVec2(paneWidth, 0),
                    start + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                    IM_COL32(0, 0, 0, 0), 4.0f);
                
                ImGui::TextUnformatted("Config sucessfully validated!!");

                if(valid) {
                    start = ImGui::GetCursorScreenPos();

                    ImGui::GetWindowDrawList()->AddLine(
                            start + ImVec2(paneWidth, 0),
                            start + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                            IM_COL32(0, 0, 0, 0), 4.0f);

                    ImGui::TextUnformatted("Graph sucessfully validated!!");
                }
            }

            for(set<string>::iterator it = errors.begin(); it != errors.end(); ++it) {
                auto start = ImGui::GetCursorScreenPos();

                ImGui::GetWindowDrawList()->AddLine(
                    start + ImVec2(paneWidth, 0),
                    start + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                    IM_COL32(0, 0, 0, 255 - (int)(255)), 4.0f);

                ImGui::TextUnformatted(string(*it).c_str());
            }


            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f
            );

            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextUnformatted("Selection");

            ImGui::BeginHorizontal("Selection Stats", ImVec2(paneWidth, 0));
            ImGui::Text("Changed %d time%s", changeCount, changeCount > 1 ? "s" : "");
            ImGui::Spring();

            if(ImGui::Button("Deselect All"))
                ed::ClearSelection();
            
            ImGui::EndHorizontal();
            ImGui::Indent();
            
            for(int i = 0; i < nodeCount; ++i) ImGui::Text("Node (%p)", selectedNodes[i].AsPointer());
            for(int i = 0; i < linkCount; ++i) ImGui::Text("Link (%p)", selectedLinks[i].AsPointer());

            ImGui::Unindent();

            if(ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z))) {
                for(auto& link : m_Links)
                    ed::Flow(link.ID);

                for(auto& link : m_AttributeLinks)
                    ed::Flow(link.ID);
            }

            if(ed::HasSelectionChanged())
                ++changeCount;

            ImGui::EndChild();
        }


        void saveJSON(Graph* graph, ofstream* out) {
            const auto ind3 = string(3, '\t');
            const auto ind2 = string(2, '\t');

            if(!graph->parent)
                *out << "{\n\t\"nodes\": [\n";

            if(graph) {
                ImVec2 pos = ed::GetNodePosition(graph->ID);
                
                *out << ind2 << "{\n" << ind3 << "\"id\": " << graph->current << ",\n" << ind3 << "\"x\": " << 
                        pos.x << ",\n" << ind3 << "\"y\": " << pos.y << '\n' << ind2 << "}";

                if(graph->current != current)
                    *out << ',';

                *out << endl;

                for(auto c : graph->childs)
                    saveJSON(c, out);
            }
        }
            

        vector<tuple<int, float, float>> loadJSON() {
            vector<tuple<int, float, float>> info;
            Document mydoc;
            ParseResult ok;

            ifstream file(json_name.c_str());

            vector<char> buf((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            buf.push_back('\0');

            file.close();
            
            ok = mydoc.Parse(&buf[0]);

            if(ok) {
                StringBuffer sb;
                Writer<StringBuffer> writer(sb);

                int id;
                float x, y;

                const Value &ns = mydoc["nodes"];

                assert(ns.IsArray());

                for(Value::ConstValueIterator it1 = ns.Begin(); it1 != ns.End(); ++it1) {
                    const Value &n = *it1;
                    assert(n.IsObject());
                    
                    for(Value::ConstMemberIterator it2 = n.MemberBegin(); it2 != n.MemberEnd(); ++it2) {              
                        const char* name = it2->name.GetString();
                        
                        if(strcmp(name, "id") == 0)
                            id = it2->value.GetInt();

                        else if(strcmp(name, "x") == 0)
                            x = it2->value.GetFloat();

                        else if(strcmp(name, "y") == 0)
                            y = it2->value.GetFloat();
                    }

                    info.push_back(make_tuple(id, x, y));
                }
 
            }
            else {
                json_ready = ok;
                errors.insert(string("Error loading JSON for node state!!"));
            }

            return info;
        }


        void saveXML(Graph* graph, ofstream* out) {
            const auto ind = string(graph->level_x, '\t');

            if(graph) {
                *out << ind << "<" << graph->node->Name;
                
                int sI = (graph->node->Inputs).size();

                if(!graph->parent && runnable)
                    *out << " graph=\"runnable\"";

                if(sI < (1 + (graph->parent ? 1 : 0) + graph->node->HasValue))
                    *out << (graph->node->Closed ? "/>" : ">") << (graph->node->HasValue ? "" : "\n");
    
                sI -= graph->node->HasValue;

                for(auto & e : graph->node->Outputs)
                    if(!e.Name.empty())
                        *out << " " << regex_replace(e.Name, regex(" = "), string("=\"")) << "\"";

                for(int i = (graph->parent ? 1 : 0); i < sI; i++)
                    *out << " " << regex_replace(((graph->node->Inputs).at(i)).Name, regex(" = "), string("=\""))
                         << (i < (sI - 1) ? "\"" : (graph->node->Closed ? "\"/>" : "\">")) << ((!graph->node->HasValue && !(i < (sI - 1))) ? "\n" : "");
            
                if(graph->node->HasValue)
                    *out << graph->node->Inputs.back().Name;
                
                else
                    for(auto c : graph->childs)
                        saveXML(c, out);

                if(!graph->node->Closed)
                    *out << (graph->node->HasValue ? "" : ind.c_str()) << "</" << graph->node->Name << ">" << endl;
            }
        }


        Graph* build(XMLElement* node, int *current, Graph* parent, bool flag) {
            Graph* temp = new Graph;

            temp->current = *current;
            temp->parent = parent;
            temp->node = NULL;

            bool isRoot = (parent) ? false : true;

            float x, y;
            vector<tuple<string, string>> attributes;

            for(const XMLAttribute* a = node->FirstAttribute(); a; a = a->Next()) {
                if(strcmp(a->Name(), "graph") == 0 && strcmp(a->Value(), "runnable") == 0)
                    runnable = true;

                else
                    attributes.push_back(make_tuple(string(a->Name()), string(a->Value())));
            }

            XMLText* value = NULL;
            bool closed = false;
            
            if(node->FirstChild())
                value = node->FirstChild()->ToText();

            if(node->ClosingType() == XMLElement::ElementClosingType::CLOSED)
                closed = true;

            temp->node = SpawnElementNode(isRoot, string(node->Name()), getAttributesTypes(configs, string(node->Name())),
                                          attributes, value ? true : false, closed);

            temp->ID = temp->node->ID;

            if(closed || value) {
                if(value && !closed)
                    temp->node->Inputs.emplace_back(GetNextId(), string(value->Value()), PinType::Function);

                temp->node->Outputs.erase(temp->node->Outputs.begin());
            }

            if(flag) {
                getPositionFromJSON(temp->current, &x, &y);
                ed::SetNodePosition(temp->ID, ImVec2(x, y));
            }

            for(XMLElement* n = node->FirstChildElement(); n; n = n->NextSiblingElement()) {
                (*current)++;
                temp->childs.push_back(build(n, current, temp, flag));
            }

            return temp;
        }


        void loadXML() {
            XMLError err = doc.LoadFile(xml_name);

            if(!err) {
                xml_ready = true;
                root_node = doc.RootElement();
            }
            
            else {
                xml_ready = false;
                errors.insert(string("Error loading XML file!!"));
            }
        }


        void showOrdinals(ImVec2 editorMin, ImVec2 editorMax) {
            int nodeCount = ed::GetNodeCount();
            std::vector<ed::NodeId> orderedNodeIds;
            
            orderedNodeIds.resize(static_cast<size_t>(nodeCount));
            ed::GetOrderedNodeIds(orderedNodeIds.data(), nodeCount);

            auto drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(editorMin, editorMax);

            int ordinal = 0;

            for(auto& nodeId : orderedNodeIds) {
                auto p0 = ed::GetNodePosition(nodeId);
                auto p1 = p0 + ed::GetNodeSize(nodeId);
                p0 = ed::CanvasToScreen(p0);
                p1 = ed::CanvasToScreen(p1);

                ImGuiTextBuffer builder;
                builder.appendf("#%d", ordinal++);

                auto textSize   = ImGui::CalcTextSize(builder.c_str());
                auto padding    = ImVec2(2.0f, 2.0f);
                auto widgetSize = textSize + padding * 2;

                auto widgetPosition = ImVec2(p1.x, p0.y) + ImVec2(0.0f, -widgetSize.y);

                drawList->AddRectFilled(widgetPosition, widgetPosition + widgetSize, IM_COL32(100, 80, 80, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                drawList->AddRect(widgetPosition, widgetPosition + widgetSize, IM_COL32(200, 160, 160, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                drawList->AddText(widgetPosition + padding, IM_COL32(255, 255, 255, 255), builder.c_str());
            }

            drawList->PopClipRect();
        }


        void OnFrame(float deltaTime) override {
            UpdateTouch();

            auto& io = ImGui::GetIO();

            if(!fileDialog.IsOpened())
                fileDialog.Open();
            
            ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

            if(json_ready && xml_changed) {
                json_info = loadJSON();

                loadXML();

                if(json_ready && xml_ready) {
                    
                    if(config_ready)
                        config_ready = loadConfig(configs, config_name, errors);
                
                    current = 0;

                    graph = build(root_node, &current, NULL, true);

                    levels_x = levelXOrder(graph);

                    link(graph, &levels_x, true);
                    linkAttributes(graph);

                    ed::NavigateToContent();
                    json_ready = false;
                    xml_ready = false;
                    
                    if(graph)
                        isClear = false;

                    if(dtd_ready)
                        parseDocument(document, dtd_name);

                    if(dtd_ready || config_ready)
                        validateGraph(graph);
                }
            }
            else if(xml_ready && xml_changed) {
                loadXML();
                
                if(xml_ready) {
                    if(config_ready)
                        config_ready = loadConfig(configs, config_name, errors);

                    current = 0;
                    graph = build(root_node, &current, NULL, false);
                    
                    levels_x = levelXOrder(graph);

                    link(graph, &levels_x, true);

                    linkAttributes(graph);

                    readjust(graph, maxWidth(&levels_x), height(graph), &levels_x);

                    ed::NavigateToContent();
                    xml_ready = false;
                    
                    if(graph)
                        isClear = false;

                    if(dtd_ready)
                        parseDocument(document, dtd_name);

                    if(dtd_ready || config_ready)
                        validateGraph(graph);
                }
            }

            BuildNodes(graph);

            for(auto & sG : secondaryGraphs)
                BuildNodes(sG);

            static ed::NodeId contextNodeId      = 0;
            static ed::LinkId contextLinkId      = 0;
            static ed::PinId  contextPinId       = 0;
            static bool createNewNode  = false;
            static Pin* newNodeLinkPin = nullptr;
            static Pin* newLinkPin     = nullptr;

            static float leftPaneWidth  = 400.0f;
            static float rightPaneWidth = 800.0f;
            Splitter(true, 4.0f, &leftPaneWidth, &rightPaneWidth, 50.0f, 50.0f);

            ShowLeftPane(leftPaneWidth - 4.0f);

            ImGui::SameLine(0.0f, 12.0f);

            ed::Begin("Node editor"); {
                auto cursorTopLeft = ImGui::GetCursorScreenPos();

                util::BlueprintNodeBuilder builder(m_HeaderBackground, GetTextureWidth(m_HeaderBackground), GetTextureHeight(m_HeaderBackground));

                /* Display Nodes */
                nodeDisplay(graph, builder, newLinkPin);

                for(auto & sG : secondaryGraphs)
                    nodeDisplay(sG, builder, newLinkPin);

                /* Display Links */
                if(!m_hideLinks) {
                    for(auto& link : m_Links)
                        ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);
                }

                for(auto& link : m_AttributeLinks)
                    ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);
                

                if(!createNewNode) {
                    
                    if(ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {

                        auto showLabel = [](const char* label, ImColor color) {
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                            auto size = ImGui::CalcTextSize(label);

                            auto padding = ImGui::GetStyle().FramePadding;
                            auto spacing = ImGui::GetStyle().ItemSpacing;

                            ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                            auto rectMin = ImGui::GetCursorScreenPos() - padding;
                            auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                            auto drawList = ImGui::GetWindowDrawList();
                            drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                            ImGui::TextUnformatted(label);
                        };

                        ed::PinId startPinId = 0, endPinId = 0;

                        if(ed::QueryNewLink(&startPinId, &endPinId)) {
                            bool foundS = false, foundE = false;
                            Pin* startPin; 
                            FindPin(graph, startPinId, &startPin, &foundS);
                            
                            if(!foundS)
                                for(auto & sG : secondaryGraphs)
                                    FindPin(sG, startPinId, &startPin, &foundS);

                            Pin* endPin; 
                            FindPin(graph, endPinId, &endPin, &foundE);

                            if(!foundE)
                                for(auto & sG : secondaryGraphs)
                                    FindPin(sG, endPinId, &endPin, &foundE);

                            newLinkPin = startPin ? startPin : endPin;

                            if(startPin->Kind == PinKind::Input) {
                                std::swap(startPin, endPin);
                                std::swap(startPinId, endPinId);
                            }

                            if(startPin && endPin) {
                                if(endPin == startPin) {
                                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                                }
                                else if(endPin->Kind == startPin->Kind) {
                                    showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                                } 
                                else if(endPin->Type != startPin->Type) {
                                    showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                                }
                                else if(isLinkedByPins(startPin, endPin)) {
                                    showLabel("x Already linked", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                                }
                                else if(startPin->Type != PinType::Flow && endPin->Type != PinType::Flow && 
                                        !CanCreateAttributeLink(startPin, endPin)) {
                                    showLabel("x Invalid Attribute Link", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                                }
                                else {
                                    showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                                    
                                    if(ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                                        if(startPin->Type == PinType::Flow && endPin->Type == PinType::Flow) {
                                            linkGraphs(startPinId, endPinId, startPin->Type);
                                            recalibrate();
                                            revalidate();
                                        }
                                        else {
                                            m_AttributeLinks.emplace_back(Link(GetNextId(), startPinId, endPinId));
                                            m_AttributeLinks.back().Color = GetIconColor(startPin->Type);
                                        }
                                    }
                                }
                            }
                        }

                        ed::PinId pinId = 0;
                        if(ed::QueryNewNode(&pinId)) {
                            bool foundN = false;
                            FindPin(graph, pinId, &newLinkPin, &foundN);

                            if(!foundN)
                                for(auto & sG : secondaryGraphs)
                                    FindPin(sG, pinId, &newLinkPin, &foundN);
                            
                            if(newLinkPin)
                                showLabel("+ Create Node", ImColor(32, 45, 32, 180));

                            if(ed::AcceptNewItem()) {
                                createNewNode  = true;
                                bool foundNN = false;
                                FindPin(graph, pinId, &newNodeLinkPin, &foundNN);

                                if(!foundNN)
                                    for(auto & sG : secondaryGraphs)
                                        FindPin(sG, pinId, &newNodeLinkPin, &foundNN);

                                newLinkPin = nullptr;
                                ed::Suspend();
                                ImGui::OpenPopup("Create New Node");
                                ed::Resume();
                            }
                        }
                    }
                    else
                        newLinkPin = nullptr;

                    ed::EndCreate();

                    if(ed::BeginDelete()) {
                        ed::LinkId linkId = 0;

                        while(ed::QueryDeletedLink(&linkId)) {
                            if(ed::AcceptDeletedItem()) {
                                bool isAtt = false;
                                Link* link = FindLink(linkId, &isAtt);

                                if(!isAtt) {
                                    Node* n = NULL;
                                    bool found = false;

                                    FindNodeWithInputPin(graph, &n, link->EndPinID, &found);
                                    
                                    bool deleted = false;

                                    lookupDeleteGraph(graph, n->ID, &deleted);

                                    recalibrate();

                                    revalidate();

                                    if(!graph)
                                        isClear = true;

                                    if(!deleted)
                                        lookupDeleteSecondaryGraph(n->ID, &deleted);
                                }
                                else
                                    DeleteLinkToPin(link->EndPinID);
                            }
                        }

                        ed::NodeId nodeId = 0;
                        while(ed::QueryDeletedNode(&nodeId)) {
                            
                            if(ed::AcceptDeletedItem()) {
                                bool deleted = false;
                                
                                lookupDeleteGraph(graph, nodeId, &deleted);
                                recalibrate();
                                revalidate();
                                
                                if(!graph)
                                    isClear = true;

                                if(!deleted)
                                    lookupDeleteSecondaryGraph(nodeId, &deleted);
                            }
                        }
                    }
                    ed::EndDelete();
                }

                ImGui::SetCursorScreenPos(cursorTopLeft);
            }

            auto openPopupPosition = ImGui::GetMousePos();
            ed::Suspend();
            
            if(ed::ShowNodeContextMenu(&contextNodeId))
                ImGui::OpenPopup("Node Context Menu");

            else if(ed::ShowPinContextMenu(&contextPinId))
                ImGui::OpenPopup("Pin Context Menu");

            else if(ed::ShowLinkContextMenu(&contextLinkId))
                ImGui::OpenPopup("Link Context Menu");

            else if(ed::ShowBackgroundContextMenu()) {
                ImGui::OpenPopup("Create New Node");
                newNodeLinkPin = nullptr;
            }

            ed::Resume();

            ed::Suspend();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            
            static bool showNodeEditing = false, stayEditing = true;

            if(ImGui::BeginPopup("Node Context Menu")) {
                bool found = false;
                Node* node = NULL;
                FindNode(graph, &node, contextNodeId, &found);                

                ImGui::TextUnformatted("Node Context Menu");
                ImGui::Separator();
                
                if(found) {
                    ImGui::Text("ID: %p", node->ID.AsPointer());
                    ImGui::Text("Type: %s", node->Type == NodeType::Element ? "Element" : "");
                    ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                    ImGui::Text("Outputs: %d", (int)node->Outputs.size());
                }
                else
                    ImGui::Text("Unknown node: %p", contextNodeId.AsPointer());
                
                if(runnable && node->Root == true) {
                    ImGui::Separator();

                    if(ImGui::MenuItem("Run")) {
                        set<int> used;
                        runGraph(graph, used);
                        revalidate();
                    }
                }

                ImGui::Separator();

                if(ImGui::MenuItem("Edit")) {
                    nodeEditing = node;
                    showNodeEditing = true;
                }

                ImGui::Separator();

                if(ImGui::MenuItem("Delete"))
                    ed::DeleteNode(contextNodeId);
                ImGui::EndPopup();
            }

            if(ImGui::BeginPopup("Pin Context Menu")) {
                bool foundP = false;
                Pin* pin; 
                
                FindPin(graph, contextPinId, &pin, &foundP);

                if(!foundP)
                    for(auto & sG : secondaryGraphs)
                        FindPin(sG, contextPinId, &pin, &foundP);

                ImGui::TextUnformatted("Pin Context Menu");
                ImGui::Separator();
                
                if(pin) {
                    ImGui::Text("ID: %p", pin->ID.AsPointer());
                    
                    if(pin->Node)
                        ImGui::Text("Node: %p", pin->Node->ID.AsPointer());
                    else
                        ImGui::Text("Node: %s", "<none>");
                }
                else
                    ImGui::Text("Unknown pin: %p", contextPinId.AsPointer());

                ImGui::EndPopup();
            }

            if(ImGui::BeginPopup("Link Context Menu")) {
                bool isAtt = false;
                auto link = FindLink(contextLinkId, &isAtt);

                ImGui::TextUnformatted("Link Context Menu");
                ImGui::Separator();
                
                if(link) {
                    ImGui::Text("ID: %p", link->ID.AsPointer());
                    ImGui::Text("From: %p", link->StartPinID.AsPointer());
                    ImGui::Text("To: %p", link->EndPinID.AsPointer());
                }
                else
                    ImGui::Text("Unknown link: %p", contextLinkId.AsPointer());
                
                ImGui::Separator();
                
                if(ImGui::MenuItem("Delete"))
                    ed::DeleteLink(contextLinkId);
                
                ImGui::EndPopup();
            }

            if(showNodeEditing)
                ShowNodeEditing(&showNodeEditing, &stayEditing, nodeEditing);


            static bool showNodeCreation = false, stay = true;
            ed::NodeId ID = 0;
            Node* node = NULL;

            if(ImGui::BeginPopup("Create New Node")) {

                if(ImGui::MenuItem("Create"))
                    showNodeCreation = true;
                
                ImGui::EndPopup();
            }

            if(showNodeCreation) {
                ShowNodeCreation(&showNodeCreation, &stay, &node);

                if(!stay && !showNodeCreation) {
                    if(node) {
                        createGraph(node);
                        BuildNode(node);

                        createNewNode = false;

                        ed::SetNodePosition(node->ID, openPopupPosition);

                        if(auto startPin = newNodeLinkPin) {
                            auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                            for(auto& pin : pins)
                                if(CanCreateLink(startPin, &pin)) {
                                    auto endPin = &pin;

                                    if(startPin->Kind == PinKind::Input)
                                        std::swap(startPin, endPin);

                                    if(startPin->Type == PinType::Flow && endPin->Type == PinType::Flow) {
                                        linkGraphs(startPin->ID, endPin->ID, startPin->Type);
                                        recalibrate();
                                        revalidate();
                                    }
                                    
                                    break;
                                }
                        }
                    }

                    stay = true;
                }
            }

            ed::Resume();
            ImGui::PopStyleVar();
            ed::End();

            auto editorMin = ImGui::GetItemRectMin();
            auto editorMax = ImGui::GetItemRectMax();
                
            if(m_ShowOrdinals)
                showOrdinals(editorMin, editorMax);

            if(m_readjust) {
                readjust(graph, maxWidth(&levels_x), height(graph), &levels_x);
                m_readjust = false;
            }

            if(m_clear) {
                release();
                m_clear = false;
            }
        }


        ImGui::FileBrowser                      fileDialog;
        XMLDocument                             doc;
        XMLElement*                             root_node = NULL;
        Graph*                                  graph = NULL;
        Node*                                   nodeEditing = NULL;
        DocumentDTD                             document = NULL;
        Configs                                 configs = NULL;
        set<string>                             errors;
        const int                               m_PinIconSize = 24;
        vector<Link>                            m_Links;
        vector<Link>                            m_AttributeLinks;
        vector<vector<int>>                     levels_x;
        ImTextureID                             m_HeaderBackground = nullptr, m_SaveIcon = nullptr, m_RestoreIcon = nullptr;
        const float                             m_TouchTime = 1.0f;
        map<ed::NodeId, float, NodeIdLess>      m_NodeTouchTime;
        bool                                    m_ShowOrdinals = false, m_hideLinks = false, m_readjust = false, m_clear = false;
};


int Main(int argc, char** argv) {
    Example example((path + string("Blueprints")).c_str(), argc, argv);

    if(example.Create())
        return example.Run();

    return 0;
}