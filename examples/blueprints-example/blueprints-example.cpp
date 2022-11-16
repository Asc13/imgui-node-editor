#include <application.h>
#include "utilities/builders.h"
#include "utilities/widgets.h"

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
#include <vector>
#include "managers/rapidxml.hpp"
#include "managers/rapidjson/document.h"
#include "managers/rapidjson/writer.h"
#include "managers/rapidjson/stringbuffer.h"


using namespace std;
using namespace rapidxml;
using namespace rapidjson;


string path = "examples/blueprints-example/";


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

static ed::EditorContext* m_Editor = nullptr;
ed::Config*               config = nullptr;


bool                 xml_ready = false;
bool                 xml_changed = false;
bool                 config_ready = false;
char                 xml_name[64] =  "";
string               json_name;
string               dtd_file = "";


enum class PinType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};


enum class PinKind {
    Output,
    Input
};


enum class NodeType {
    Blueprint,
    Simple,
    Tree,
    Comment,
    Houdini
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
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    std::string State;
    std::string SavedState;

    Node(int id, string name, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color), Type(NodeType::Blueprint), Size(0, 0) {}
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
    ImVec2 Size;
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
            
            for (auto& entry : m_NodeTouchTime) {
                if(entry.second > 0.0f)
                    entry.second -= deltaTime;
            }
        }


        Node* FindNode(ed::NodeId id) {
            for(auto& node : m_Nodes)
                if(node.ID == id)
                    return &node;

            return nullptr;
        }


        Link* FindLink(ed::LinkId id) {
            for(auto& link : m_Links)
                if(link.ID == id)
                    return &link;

            return nullptr;
        }


        Pin* FindPin(ed::PinId id) {
            if(!id)
                return nullptr;

            for(auto& node : m_Nodes) {
                for(auto& pin : node.Inputs)
                    if(pin.ID == id)
                        return &pin;

                for(auto& pin : node.Outputs)
                    if(pin.ID == id)
                        return &pin;
            }

            return nullptr;
        }


        bool IsPinLinked(ed::PinId id) {
            if(!id)
                return false;

            for(auto& link : m_Links)
                if(link.StartPinID == id || link.EndPinID == id)
                    return true;

            return false;
        }


        bool CanCreateLink(Pin* a, Pin* b) {
            if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
                return false;

            return true;
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


        Node* SpawnValueNode(string value) {
            m_Nodes.emplace_back(GetNextId(), "");
            m_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
            m_Nodes.back().Inputs.emplace_back(GetNextId(), value, PinType::String);

            BuildNode(&m_Nodes.back());

            return &m_Nodes.back();
        }


        Node* SpawnAttributeNode(bool isRoot, string name, const xml_node<>* node) {
            m_Nodes.emplace_back(GetNextId(), name);

            if(!isRoot)
                m_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);

            for(const xml_attribute<>* a = node->first_attribute(); a; a = a->next_attribute()) {
                m_Nodes.back().Inputs.emplace_back(GetNextId(), a->name() + string(" = ") + a->value(), PinType::String);
            }

            m_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);

            BuildNode(&m_Nodes.back());

            return &m_Nodes.back();
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

                    for(auto childs : c->childs)
                        main.push(childs);
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


        void readjust(Graph* node, int max_witdh, int max_heigth, vector<vector<int>>* levels_x) {
            bool isRoot = (node->parent) ? false : true;

            float tam_max_y = (300.0 * (float) max_witdh);
            float x, y;

            if(isRoot) {
                x = 0.0; y = tam_max_y / 2.0;
            }
            else {
                x = 400.0 * ((float) node->level_x); 
                y = (tam_max_y / (width(levels_x, node) + 1.0)) * ((float) node->level_y + 1.0);
            }

            ed::SetNodePosition(node->ID, ImVec2(x, y));

            for(auto n : node->childs)
                readjust(n, max_witdh, max_heigth, levels_x);
        }


        void release() {
            m_NextId = 1;
            m_Nodes.clear();
            m_Links.clear();
            levels_x.clear();

            if(config)
                delete config;
                config = new ed::Config;

            if(graph)
                delete graph;
                graph = new Graph;

            if(&doc) {
                doc.clear();
                root_node = NULL;
            }
        }


        void printNode(Graph* node) {
            cout << "ID: " << node->ID.Get() << endl;
            cout << "Current: " << node->current << endl;
            cout << "Parent: " << ((node->parent) ? (node->parent)->current : -1) << endl;
            cout << "Level X: " << node->level_x << endl;
            cout << "Level Y: " << node->level_y << endl;
            cout << "Size: " << node->Size.x << " " << node->Size.y << endl;
            cout << endl;
        }


        void link(Graph* node, vector<vector<int>> *levels_x) {
            int current = node->current;
            int parent = (node->parent) ? (node->parent)->current : -1;
            
            for(int i = 0; i < (*levels_x).size(); ++i) {
                for(auto l : (*levels_x).at(i))
                    if(l == current) {
                        node->level_x = i;
                        break;
                    }
            }

            node->level_y = levelYOrder(node, levels_x);

            // printNode(node);

            if(node->parent)
                m_Links.push_back(Link(GetNextLinkId(), m_Nodes[parent].Outputs[0].ID, m_Nodes[current].Inputs[0].ID));

            for(auto n : node->childs)
                link(n, levels_x);
        }


        Graph* build(const xml_node<>* node, int *current, Graph* parent) {
            Graph* temp = new Graph;
            
            temp->current = *current;
            temp->parent = parent;

            bool isRoot = parent ? false : true;
            Node* imgui_node;

            switch(node->type()) {
                case node_element:
                    imgui_node = SpawnAttributeNode(isRoot, node->name(), node);
                    temp->ID = imgui_node->ID;
                    temp->Size = imgui_node->Size;

                    for(const xml_node<>* n = node->first_node(); n; n = n->next_sibling()) {
                        (*current)++;
                        (temp->childs).push_back(build(n, current, temp));
                    }

                    break;

                case node_data:
                    imgui_node = SpawnValueNode(node->value());
                    temp->ID = imgui_node->ID;
                    temp->Size = imgui_node->Size;
                    break;
            
                default:

                    break;
            }

            return temp;
        }


        void BuildNodes() {
            for (auto& node : m_Nodes)
                BuildNode(&node);
        }


        void OnStart() override {
            m_Editor = ed::CreateEditor(config);
            ed::SetCurrentEditor(m_Editor);

            m_HeaderBackground = LoadTexture((path + string("/data/BlueprintBackground.png")).c_str());
            m_SaveIcon         = LoadTexture((path + string("/data/ic_save_white_24dp.png")).c_str());
            m_RestoreIcon      = LoadTexture((path + string("/data/ic_restore_white_24dp.png")).c_str());
        }


        void OnStop() override {
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
            ifstream f((path + name).c_str());
            return f.good();
        }


        void ShowXmlReader(bool* show = nullptr) {
            if(!ImGui::Begin("XMLReader", show)) {
                ImGui::End();
                return;
            }

            char temp_name[64];
            strcpy(temp_name, xml_name);

            auto paneWidth = ImGui::GetContentRegionAvail().x;

            auto& editorStyle = ed::GetStyle();
            ImGui::BeginHorizontal("XMLReader File Chooser", ImVec2(paneWidth + 200, 0), 1.0f);
            ImGui::TextUnformatted("File name");
            ImGui::Spring();
            ImGui::EndHorizontal();
            ImGui::Spacing();
            ImGui::InputText("", xml_name, IM_ARRAYSIZE(xml_name));

            json_name = regex_replace(string(xml_name), regex("xml"), string("json"));

            if(fileExists(xml_name) && regex_search(string(xml_name), regex(".xml"))) {
                if(fileExists(json_name) && regex_search(json_name, regex(".json"))) {
                    xml_ready = false; config_ready = true;
                }
                else {
                    xml_ready = true; 
                }
            }
            else {
                xml_ready = false; config_ready = false;
            }

            if(strcmp(temp_name, xml_name) == 0)
                xml_changed = false;
            else
                xml_changed = true; 

            ImGui::End();
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
            if (ImGui::Button("Reset to defaults"))
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
            for (int i = 0; i < ed::StyleColor_Count; ++i)
            {
                auto name = ed::GetStyleColorName((ed::StyleColor)i);
                if (!filter.PassFilter(name))
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

            static bool showStyleEditor = false, showXMLReader = false;

            ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, 0));
            ImGui::Spring(0.0f, 0.0f);
            
            if(ImGui::Button("Zoom to Content"))
                ed::NavigateToContent();

            ImGui::Spring(0.0f);
            
            if(ImGui::Button("Show Flow"))
                for (auto& link : m_Links)
                    ed::Flow(link.ID);

            ImGui::Spring(0.0f);

            if(ImGui::Button("XMLReader"))
                showXMLReader = true;

            ImGui::Spring(0.0f);

            if(ImGui::Button("Edit Style"))
                showStyleEditor = true;

            ImGui::EndHorizontal();

            ImGui::BeginHorizontal("Style Editor", ImVec2(paneWidth, -200));
            ImGui::Spring(0.0f, 0.0f);
            ImGui::Checkbox("Show Ordinals", &m_ShowOrdinals);
            ImGui::Spring(0.0f);

            if(ImGui::Button("Save") && regex_search(json_name, regex(".json"))) {
                config->SettingsFile = (path + json_name);

                config->UserPointer = this;
                
                config->SaveNodeSettings = [](ed::NodeId nodeId, const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer) -> bool {
                    auto self = static_cast<Example*>(userPointer);

                    auto node = self->FindNode(nodeId);

                    if(!node)
                        return false;

                    node->State.assign(data, size);

                    self->TouchNode(nodeId);

                    return true;
                };

                //printConfig(false);
            }

            

            ImGui::EndHorizontal();

            if(showXMLReader)
                ShowXmlReader(&showXMLReader);

            if(showStyleEditor)
                ShowStyleEditor(&showStyleEditor);


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
                ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextUnformatted("Nodes");
            ImGui::Indent();
            for (auto& node : m_Nodes)
            {
                ImGui::PushID(node.ID.AsPointer());
                auto start = ImGui::GetCursorScreenPos();

                if (const auto progress = GetTouchProgress(node.ID))
                {
                    ImGui::GetWindowDrawList()->AddLine(
                        start + ImVec2(-8, 0),
                        start + ImVec2(-8, ImGui::GetTextLineHeight()),
                        IM_COL32(255, 0, 0, 255 - (int)(255 * progress)), 4.0f);
                }

                bool isSelected = std::find(selectedNodes.begin(), selectedNodes.end(), node.ID) != selectedNodes.end();
                if (ImGui::Selectable((node.Name + "##" + std::to_string(reinterpret_cast<uintptr_t>(node.ID.AsPointer()))).c_str(), &isSelected))
                {
                    if (io.KeyCtrl)
                    {
                        if (isSelected)
                            ed::SelectNode(node.ID, true);
                        else
                            ed::DeselectNode(node.ID);
                    }
                    else
                        ed::SelectNode(node.ID, false);

                    ed::NavigateToSelection();
                }
                if (ImGui::IsItemHovered() && !node.State.empty())
                    ImGui::SetTooltip("State: %s", node.State.c_str());

                auto id = std::string("(") + std::to_string(reinterpret_cast<uintptr_t>(node.ID.AsPointer())) + ")";
                auto textSize = ImGui::CalcTextSize(id.c_str(), nullptr);
                auto iconPanelPos = start + ImVec2(
                    paneWidth - ImGui::GetStyle().FramePadding.x - ImGui::GetStyle().IndentSpacing - saveIconWidth - restoreIconWidth - ImGui::GetStyle().ItemInnerSpacing.x * 1,
                    (ImGui::GetTextLineHeight() - saveIconHeight) / 2);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(iconPanelPos.x - textSize.x - ImGui::GetStyle().ItemInnerSpacing.x, start.y),
                    IM_COL32(255, 255, 255, 255), id.c_str(), nullptr);

                auto drawList = ImGui::GetWindowDrawList();
                ImGui::SetCursorScreenPos(iconPanelPos);
                ImGui::SetItemAllowOverlap();
                if (node.SavedState.empty())
                {
                    if (ImGui::InvisibleButton("save", ImVec2((float)saveIconWidth, (float)saveIconHeight)))
                        node.SavedState = node.State;

                    if (ImGui::IsItemActive())
                        drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
                    else if (ImGui::IsItemHovered())
                        drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
                    else
                        drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
                }
                else
                {
                    ImGui::Dummy(ImVec2((float)saveIconWidth, (float)saveIconHeight));
                    drawList->AddImage(m_SaveIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
                }

                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::SetItemAllowOverlap();
                if (!node.SavedState.empty())
                {
                    if (ImGui::InvisibleButton("restore", ImVec2((float)restoreIconWidth, (float)restoreIconHeight)))
                    {
                        node.State = node.SavedState;
                        ed::RestoreNodeState(node.ID);
                        node.SavedState.clear();
                    }

                    if (ImGui::IsItemActive())
                        drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 96));
                    else if (ImGui::IsItemHovered())
                        drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
                    else
                        drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
                }
                else
                {
                    ImGui::Dummy(ImVec2((float)restoreIconWidth, (float)restoreIconHeight));
                    drawList->AddImage(m_RestoreIcon, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 32));
                }

                ImGui::SameLine(0, 0);
                ImGui::SetItemAllowOverlap();
                ImGui::Dummy(ImVec2(0, (float)restoreIconHeight));

                ImGui::PopID();
            }
            ImGui::Unindent();

            static int changeCount = 0;

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImGui::GetCursorScreenPos() + ImVec2(paneWidth, ImGui::GetTextLineHeight()),
                ImColor(ImGui::GetStyle().Colors[ImGuiCol_HeaderActive]), ImGui::GetTextLineHeight() * 0.25f);
            ImGui::Spacing(); ImGui::SameLine();
            ImGui::TextUnformatted("Selection");

            ImGui::BeginHorizontal("Selection Stats", ImVec2(paneWidth, 0));
            ImGui::Text("Changed %d time%s", changeCount, changeCount > 1 ? "s" : "");
            ImGui::Spring();
            if (ImGui::Button("Deselect All"))
                ed::ClearSelection();
            ImGui::EndHorizontal();
            ImGui::Indent();
            for (int i = 0; i < nodeCount; ++i) ImGui::Text("Node (%p)", selectedNodes[i].AsPointer());
            for (int i = 0; i < linkCount; ++i) ImGui::Text("Link (%p)", selectedLinks[i].AsPointer());
            ImGui::Unindent();

            if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
                for (auto& link : m_Links)
                    ed::Flow(link.ID);

            if (ed::HasSelectionChanged())
                ++changeCount;

            ImGui::EndChild();
        }


        void saveJSON(Graph* graph) {

            if(graph){

                ImVec2 pos = ed::getNodePosition(graph->ID);
                
                fd << "{\n" << "\tid = " << graph->ID << endl <<"\tx = " << pos.x << endl <<"\ty = " << pos.y << "\n},\n"

                for(auto c : graph->childs)
                    saveJSON(c);

            }
                
        }
            

        vector<tuple<int, float, float>> loadJSON() {
            ifstream file((path + string(json_name).c_str()));

            vector<char> buf((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            buf.push_back('\0');

            Document mydoc;
            mydoc.Parse(&buf[0]);
            StringBuffer sb;
            Writer<StringBuffer> writer(sb);

            vector<tuple<int, float, float>> info;
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

            return info;
        }


        void saveXML() {

        }

        void loadXML() {
            ifstream file((path + string(xml_name).c_str()));

            vector<char> buffer((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            buffer.push_back('\0');
            doc.parse<0>(&buffer[0]);

            root_node = doc.first_node();
        }


        void OnFrame(float deltaTime) override {
            UpdateTouch();

            auto& io = ImGui::GetIO();
            
            ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

            if(config_ready && xml_changed) {

                //printConfig(true);

                vector<tuple<int, float, float>> json_info = loadJSON();
            }
            else if(xml_ready && xml_changed) {
                loadXML();
                
                int current = 0;
                graph = build(root_node, &current, NULL);

                levels_x = levelXOrder(graph);
                link(graph, &levels_x);

                readjust(graph, maxWidth(&levels_x), height(graph), &levels_x);

            }
            else if(!xml_ready && !xml_changed && !config_ready)
                release();


            ed::NavigateToContent();
            BuildNodes();

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

                for (auto& node : m_Nodes) {
                    if (node.Type != NodeType::Blueprint && node.Type != NodeType::Simple)
                        continue;

                    const auto isSimple = node.Type == NodeType::Simple;

                    bool hasOutputDelegates = false;
                    for (auto& output : node.Outputs)
                        if (output.Type == PinType::Delegate)
                            hasOutputDelegates = true;

                    builder.Begin(node.ID);
                        if (!isSimple)
                        {
                            builder.Header(node.Color);
                                ImGui::Spring(0);
                                ImGui::TextUnformatted(node.Name.c_str());
                                ImGui::Spring(1);
                                ImGui::Dummy(ImVec2(0, 28));
                                if (hasOutputDelegates)
                                {
                                    ImGui::BeginVertical("delegates", ImVec2(0, 28));
                                    ImGui::Spring(1, 0);
                                    for (auto& output : node.Outputs)
                                    {
                                        if (output.Type != PinType::Delegate)
                                            continue;

                                        auto alpha = ImGui::GetStyle().Alpha;
                                        if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                                            alpha = alpha * (48.0f / 255.0f);

                                        ed::BeginPin(output.ID, ed::PinKind::Output);
                                        ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
                                        ed::PinPivotSize(ImVec2(0, 0));
                                        ImGui::BeginHorizontal(output.ID.AsPointer());
                                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                                        if (!output.Name.empty())
                                        {
                                            ImGui::TextUnformatted(output.Name.c_str());
                                            ImGui::Spring(0);
                                        }
                                        DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
                                        ImGui::Spring(0, ImGui::GetStyle().ItemSpacing.x / 2);
                                        ImGui::EndHorizontal();
                                        ImGui::PopStyleVar();
                                        ed::EndPin();
                                    }
                                    ImGui::Spring(1, 0);
                                    ImGui::EndVertical();
                                    ImGui::Spring(0, ImGui::GetStyle().ItemSpacing.x / 2);
                                }
                                else
                                    ImGui::Spring(0);
                            builder.EndHeader();
                        }

                        for (auto& input : node.Inputs)
                        {
                            auto alpha = ImGui::GetStyle().Alpha;
                            if (newLinkPin && !CanCreateLink(newLinkPin, &input) && &input != newLinkPin)
                                alpha = alpha * (48.0f / 255.0f);

                            builder.Input(input.ID);
                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                            DrawPinIcon(input, IsPinLinked(input.ID), (int)(alpha * 255));
                            ImGui::Spring(0);
                            if (!input.Name.empty())
                            {
                                ImGui::TextUnformatted(input.Name.c_str());
                                ImGui::Spring(0);
                            }
                            if (input.Type == PinType::Bool)
                            {
                                ImGui::Button("Hello");
                                ImGui::Spring(0);
                            }
                            ImGui::PopStyleVar();
                            builder.EndInput();
                        }

                        if (isSimple)
                        {
                            builder.Middle();

                            ImGui::Spring(1, 0);
                            ImGui::TextUnformatted(node.Name.c_str());
                            ImGui::Spring(1, 0);
                        }

                        for (auto& output : node.Outputs)
                        {
                            if (!isSimple && output.Type == PinType::Delegate)
                                continue;

                            auto alpha = ImGui::GetStyle().Alpha;
                            if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                                alpha = alpha * (48.0f / 255.0f);

                            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                            builder.Output(output.ID);
                            if (output.Type == PinType::String)
                            {
                                static char buffer[128] = "Edit Me\nMultiline!";
                                static bool wasActive = false;

                                ImGui::PushItemWidth(100.0f);
                                ImGui::InputText("##edit", buffer, 127);
                                ImGui::PopItemWidth();
                                if (ImGui::IsItemActive() && !wasActive)
                                {
                                    ed::EnableShortcuts(false);
                                    wasActive = true;
                                }
                                else if (!ImGui::IsItemActive() && wasActive)
                                {
                                    ed::EnableShortcuts(true);
                                    wasActive = false;
                                }
                                ImGui::Spring(0);
                            }
                            if (!output.Name.empty())
                            {
                                ImGui::Spring(0);
                                ImGui::TextUnformatted(output.Name.c_str());
                            }
                            ImGui::Spring(0);
                            DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
                            ImGui::PopStyleVar();
                            builder.EndOutput();
                        }

                    builder.End();
                }

                for (auto& node : m_Nodes)
                {
                    if (node.Type != NodeType::Tree)
                        continue;

                    const float rounding = 5.0f;
                    const float padding  = 12.0f;

                    const auto pinBackground = ed::GetStyle().Colors[ed::StyleColor_NodeBg];

                    ed::PushStyleColor(ed::StyleColor_NodeBg,        ImColor(128, 128, 128, 200));
                    ed::PushStyleColor(ed::StyleColor_NodeBorder,    ImColor( 32,  32,  32, 200));
                    ed::PushStyleColor(ed::StyleColor_PinRect,       ImColor( 60, 180, 255, 150));
                    ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor( 60, 180, 255, 150));

                    ed::PushStyleVar(ed::StyleVar_NodePadding,  ImVec4(0, 0, 0, 0));
                    ed::PushStyleVar(ed::StyleVar_NodeRounding, rounding);
                    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f,  1.0f));
                    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
                    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);
                    ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);
                    ed::PushStyleVar(ed::StyleVar_PinRadius, 5.0f);
                    ed::BeginNode(node.ID);

                    ImGui::BeginVertical(node.ID.AsPointer());
                    ImGui::BeginHorizontal("inputs");
                    ImGui::Spring(0, padding * 2);

                    ImRect inputsRect;
                    int inputAlpha = 200;
                    if (!node.Inputs.empty())
                    {
                            auto& pin = node.Inputs[0];
                            ImGui::Dummy(ImVec2(0, padding));
                            ImGui::Spring(1, 0);
                            inputsRect = ImGui_GetItemRect();

                            ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
                            ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
    #if IMGUI_VERSION_NUM > 18101
                            ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersBottom);
    #else
                            ed::PushStyleVar(ed::StyleVar_PinCorners, 12);
    #endif
                            ed::BeginPin(pin.ID, ed::PinKind::Input);
                            ed::PinPivotRect(inputsRect.GetTL(), inputsRect.GetBR());
                            ed::PinRect(inputsRect.GetTL(), inputsRect.GetBR());
                            ed::EndPin();
                            ed::PopStyleVar(3);

                            if (newLinkPin && !CanCreateLink(newLinkPin, &pin) && &pin != newLinkPin)
                                inputAlpha = (int)(255 * ImGui::GetStyle().Alpha * (48.0f / 255.0f));
                    }
                    else
                        ImGui::Dummy(ImVec2(0, padding));

                    ImGui::Spring(0, padding * 2);
                    ImGui::EndHorizontal();

                    ImGui::BeginHorizontal("content_frame");
                    ImGui::Spring(1, padding);

                    ImGui::BeginVertical("content", ImVec2(0.0f, 0.0f));
                    ImGui::Dummy(ImVec2(160, 0));
                    ImGui::Spring(1);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    ImGui::EndVertical();
                    auto contentRect = ImGui_GetItemRect();

                    ImGui::Spring(1, padding);
                    ImGui::EndHorizontal();

                    ImGui::BeginHorizontal("outputs");
                    ImGui::Spring(0, padding * 2);

                    ImRect outputsRect;
                    int outputAlpha = 200;
                    if (!node.Outputs.empty())
                    {
                        auto& pin = node.Outputs[0];
                        ImGui::Dummy(ImVec2(0, padding));
                        ImGui::Spring(1, 0);
                        outputsRect = ImGui_GetItemRect();

    #if IMGUI_VERSION_NUM > 18101
                        ed::PushStyleVar(ed::StyleVar_PinCorners, ImDrawFlags_RoundCornersTop);
    #else
                        ed::PushStyleVar(ed::StyleVar_PinCorners, 3);
    #endif
                        ed::BeginPin(pin.ID, ed::PinKind::Output);
                        ed::PinPivotRect(outputsRect.GetTL(), outputsRect.GetBR());
                        ed::PinRect(outputsRect.GetTL(), outputsRect.GetBR());
                        ed::EndPin();
                        ed::PopStyleVar();

                        if (newLinkPin && !CanCreateLink(newLinkPin, &pin) && &pin != newLinkPin)
                            outputAlpha = (int)(255 * ImGui::GetStyle().Alpha * (48.0f / 255.0f));
                    }
                    else
                        ImGui::Dummy(ImVec2(0, padding));

                    ImGui::Spring(0, padding * 2);
                    ImGui::EndHorizontal();

                    ImGui::EndVertical();

                    ed::EndNode();
                    ed::PopStyleVar(7);
                    ed::PopStyleColor(4);

                    auto drawList = ed::GetNodeBackgroundDrawList(node.ID);

    #if IMGUI_VERSION_NUM > 18101
                    const auto    topRoundCornersFlags = ImDrawFlags_RoundCornersTop;
                    const auto bottomRoundCornersFlags = ImDrawFlags_RoundCornersBottom;
    #else
                    const auto    topRoundCornersFlags = 1 | 2;
                    const auto bottomRoundCornersFlags = 4 | 8;
    #endif

                    drawList->AddRectFilled(inputsRect.GetTL() + ImVec2(0, 1), inputsRect.GetBR(),
                        IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, bottomRoundCornersFlags);
                    //ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
                    drawList->AddRect(inputsRect.GetTL() + ImVec2(0, 1), inputsRect.GetBR(),
                        IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, bottomRoundCornersFlags);
                    //ImGui::PopStyleVar();
                    drawList->AddRectFilled(outputsRect.GetTL(), outputsRect.GetBR() - ImVec2(0, 1),
                        IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, topRoundCornersFlags);
                    //ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
                    drawList->AddRect(outputsRect.GetTL(), outputsRect.GetBR() - ImVec2(0, 1),
                        IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, topRoundCornersFlags);
                    //ImGui::PopStyleVar();
                    drawList->AddRectFilled(contentRect.GetTL(), contentRect.GetBR(), IM_COL32(24, 64, 128, 200), 0.0f);
                    //ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
                    drawList->AddRect(
                        contentRect.GetTL(),
                        contentRect.GetBR(),
                        IM_COL32(48, 128, 255, 100), 0.0f);
                    //ImGui::PopStyleVar();
                }

                for (auto& node : m_Nodes)
                {
                    if (node.Type != NodeType::Houdini)
                        continue;

                    const float rounding = 10.0f;
                    const float padding  = 12.0f;


                    ed::PushStyleColor(ed::StyleColor_NodeBg,        ImColor(229, 229, 229, 200));
                    ed::PushStyleColor(ed::StyleColor_NodeBorder,    ImColor(125, 125, 125, 200));
                    ed::PushStyleColor(ed::StyleColor_PinRect,       ImColor(229, 229, 229, 60));
                    ed::PushStyleColor(ed::StyleColor_PinRectBorder, ImColor(125, 125, 125, 60));

                    const auto pinBackground = ed::GetStyle().Colors[ed::StyleColor_NodeBg];

                    ed::PushStyleVar(ed::StyleVar_NodePadding,  ImVec4(0, 0, 0, 0));
                    ed::PushStyleVar(ed::StyleVar_NodeRounding, rounding);
                    ed::PushStyleVar(ed::StyleVar_SourceDirection, ImVec2(0.0f,  1.0f));
                    ed::PushStyleVar(ed::StyleVar_TargetDirection, ImVec2(0.0f, -1.0f));
                    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);
                    ed::PushStyleVar(ed::StyleVar_PinBorderWidth, 1.0f);
                    ed::PushStyleVar(ed::StyleVar_PinRadius, 6.0f);
                    ed::BeginNode(node.ID);

                    ImGui::BeginVertical(node.ID.AsPointer());
                    if (!node.Inputs.empty())
                    {
                        ImGui::BeginHorizontal("inputs");
                        ImGui::Spring(1, 0);

                        ImRect inputsRect;
                        int inputAlpha = 200;
                        for (auto& pin : node.Inputs)
                        {
                            ImGui::Dummy(ImVec2(padding, padding));
                            inputsRect = ImGui_GetItemRect();
                            ImGui::Spring(1, 0);
                            inputsRect.Min.y -= padding;
                            inputsRect.Max.y -= padding;

    #if IMGUI_VERSION_NUM > 18101
                            const auto allRoundCornersFlags = ImDrawFlags_RoundCornersAll;
    #else
                            const auto allRoundCornersFlags = 15;
    #endif
                            //ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
                            //ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
                            ed::PushStyleVar(ed::StyleVar_PinCorners, allRoundCornersFlags);

                            ed::BeginPin(pin.ID, ed::PinKind::Input);
                            ed::PinPivotRect(inputsRect.GetCenter(), inputsRect.GetCenter());
                            ed::PinRect(inputsRect.GetTL(), inputsRect.GetBR());
                            ed::EndPin();
                            //ed::PopStyleVar(3);
                            ed::PopStyleVar(1);

                            auto drawList = ImGui::GetWindowDrawList();
                            drawList->AddRectFilled(inputsRect.GetTL(), inputsRect.GetBR(),
                                IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, allRoundCornersFlags);
                            drawList->AddRect(inputsRect.GetTL(), inputsRect.GetBR(),
                                IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), inputAlpha), 4.0f, allRoundCornersFlags);

                            if (newLinkPin && !CanCreateLink(newLinkPin, &pin) && &pin != newLinkPin)
                                inputAlpha = (int)(255 * ImGui::GetStyle().Alpha * (48.0f / 255.0f));
                        }

                        //ImGui::Spring(1, 0);
                        ImGui::EndHorizontal();
                    }

                    ImGui::BeginHorizontal("content_frame");
                    ImGui::Spring(1, padding);

                    ImGui::BeginVertical("content", ImVec2(0.0f, 0.0f));
                    ImGui::Dummy(ImVec2(160, 0));
                    ImGui::Spring(1);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Spring(1);
                    ImGui::EndVertical();
                    auto contentRect = ImGui_GetItemRect();

                    ImGui::Spring(1, padding);
                    ImGui::EndHorizontal();

                    if (!node.Outputs.empty())
                    {
                        ImGui::BeginHorizontal("outputs");
                        ImGui::Spring(1, 0);

                        ImRect outputsRect;
                        int outputAlpha = 200;
                        for (auto& pin : node.Outputs)
                        {
                            ImGui::Dummy(ImVec2(padding, padding));
                            outputsRect = ImGui_GetItemRect();
                            ImGui::Spring(1, 0);
                            outputsRect.Min.y += padding;
                            outputsRect.Max.y += padding;

    #if IMGUI_VERSION_NUM > 18101
                            const auto allRoundCornersFlags = ImDrawFlags_RoundCornersAll;
                            const auto topRoundCornersFlags = ImDrawFlags_RoundCornersTop;
    #else
                            const auto allRoundCornersFlags = 15;
                            const auto topRoundCornersFlags = 3;
    #endif

                            ed::PushStyleVar(ed::StyleVar_PinCorners, topRoundCornersFlags);
                            ed::BeginPin(pin.ID, ed::PinKind::Output);
                            ed::PinPivotRect(outputsRect.GetCenter(), outputsRect.GetCenter());
                            ed::PinRect(outputsRect.GetTL(), outputsRect.GetBR());
                            ed::EndPin();
                            ed::PopStyleVar();


                            auto drawList = ImGui::GetWindowDrawList();
                            drawList->AddRectFilled(outputsRect.GetTL(), outputsRect.GetBR(),
                                IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, allRoundCornersFlags);
                            drawList->AddRect(outputsRect.GetTL(), outputsRect.GetBR(),
                                IM_COL32((int)(255 * pinBackground.x), (int)(255 * pinBackground.y), (int)(255 * pinBackground.z), outputAlpha), 4.0f, allRoundCornersFlags);


                            if (newLinkPin && !CanCreateLink(newLinkPin, &pin) && &pin != newLinkPin)
                                outputAlpha = (int)(255 * ImGui::GetStyle().Alpha * (48.0f / 255.0f));
                        }

                        ImGui::EndHorizontal();
                    }

                    ImGui::EndVertical();

                    ed::EndNode();
                    ed::PopStyleVar(7);
                    ed::PopStyleColor(4);
                }

                for (auto& node : m_Nodes)
                {
                    if (node.Type != NodeType::Comment)
                        continue;

                    const float commentAlpha = 0.75f;

                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, commentAlpha);
                    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 64));
                    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 64));
                    ed::BeginNode(node.ID);
                    ImGui::PushID(node.ID.AsPointer());
                    ImGui::BeginVertical("content");
                    ImGui::BeginHorizontal("horizontal");
                    ImGui::Spring(1);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    ImGui::EndHorizontal();
                    ed::Group(node.Size);
                    ImGui::EndVertical();
                    ImGui::PopID();
                    ed::EndNode();
                    ed::PopStyleColor(2);
                    ImGui::PopStyleVar();

                    if (ed::BeginGroupHint(node.ID)) {                 
                        auto bgAlpha = static_cast<int>(ImGui::GetStyle().Alpha * 255);

                        auto min = ed::GetGroupMin();

                        ImGui::SetCursorScreenPos(min - ImVec2(-8, ImGui::GetTextLineHeightWithSpacing() + 4));
                        ImGui::BeginGroup();
                        ImGui::TextUnformatted(node.Name.c_str());
                        ImGui::EndGroup();

                        auto drawList = ed::GetHintBackgroundDrawList();

                        auto hintBounds      = ImGui_GetItemRect();
                        auto hintFrameBounds = ImRect_Expanded(hintBounds, 8, 4);

                        drawList->AddRectFilled(
                            hintFrameBounds.GetTL(),
                            hintFrameBounds.GetBR(),
                            IM_COL32(255, 255, 255, 64 * bgAlpha / 255), 4.0f);

                        drawList->AddRect(
                            hintFrameBounds.GetTL(),
                            hintFrameBounds.GetBR(),
                            IM_COL32(255, 255, 255, 128 * bgAlpha / 255), 4.0f);

                        //ImGui::PopStyleVar();
                    }
                    ed::EndGroupHint();
                }

                for (auto& link : m_Links)
                    ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

                if (!createNewNode) {
                    if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
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
                        if (ed::QueryNewLink(&startPinId, &endPinId)) {
                            auto startPin = FindPin(startPinId);
                            auto endPin   = FindPin(endPinId);

                            newLinkPin = startPin ? startPin : endPin;

                            if (startPin->Kind == PinKind::Input) {
                                std::swap(startPin, endPin);
                                std::swap(startPinId, endPinId);
                            }

                            if (startPin && endPin) {
                                if (endPin == startPin)
                                {
                                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                                }
                                else if (endPin->Kind == startPin->Kind)
                                {
                                    showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                                }
                                else if (endPin->Type != startPin->Type)
                                {
                                    showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                                    ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                                }
                                else
                                {
                                    showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                                    {
                                        m_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                                        m_Links.back().Color = GetIconColor(startPin->Type);
                                    }
                                }
                            }
                        }

                        ed::PinId pinId = 0;
                        if (ed::QueryNewNode(&pinId))
                        {
                            newLinkPin = FindPin(pinId);
                            if (newLinkPin)
                                showLabel("+ Create Node", ImColor(32, 45, 32, 180));

                            if (ed::AcceptNewItem())
                            {
                                createNewNode  = true;
                                newNodeLinkPin = FindPin(pinId);
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

                    if (ed::BeginDelete())
                    {
                        ed::LinkId linkId = 0;
                        while (ed::QueryDeletedLink(&linkId))
                        {
                            if (ed::AcceptDeletedItem())
                            {
                                auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                                if (id != m_Links.end())
                                    m_Links.erase(id);
                            }
                        }

                        ed::NodeId nodeId = 0;
                        while (ed::QueryDeletedNode(&nodeId))
                        {
                            if (ed::AcceptDeletedItem())
                            {
                                auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                                if (id != m_Nodes.end())
                                    m_Nodes.erase(id);
                            }
                        }
                    }
                    ed::EndDelete();
                }

                ImGui::SetCursorScreenPos(cursorTopLeft);
            }

        # if 1
            auto openPopupPosition = ImGui::GetMousePos();
            ed::Suspend();
            if (ed::ShowNodeContextMenu(&contextNodeId))
                ImGui::OpenPopup("Node Context Menu");
            else if (ed::ShowPinContextMenu(&contextPinId))
                ImGui::OpenPopup("Pin Context Menu");
            else if (ed::ShowLinkContextMenu(&contextLinkId))
                ImGui::OpenPopup("Link Context Menu");
            else if (ed::ShowBackgroundContextMenu())
            {
                ImGui::OpenPopup("Create New Node");
                newNodeLinkPin = nullptr;
            }
            ed::Resume();

            ed::Suspend();
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            if (ImGui::BeginPopup("Node Context Menu"))
            {
                auto node = FindNode(contextNodeId);

                ImGui::TextUnformatted("Node Context Menu");
                ImGui::Separator();
                if (node)
                {
                    ImGui::Text("ID: %p", node->ID.AsPointer());
                    ImGui::Text("Type: %s", node->Type == NodeType::Blueprint ? "Blueprint" : (node->Type == NodeType::Tree ? "Tree" : "Comment"));
                    ImGui::Text("Inputs: %d", (int)node->Inputs.size());
                    ImGui::Text("Outputs: %d", (int)node->Outputs.size());
                }
                else
                    ImGui::Text("Unknown node: %p", contextNodeId.AsPointer());
                ImGui::Separator();
                if (ImGui::MenuItem("Delete"))
                    ed::DeleteNode(contextNodeId);
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Pin Context Menu"))
            {
                auto pin = FindPin(contextPinId);

                ImGui::TextUnformatted("Pin Context Menu");
                ImGui::Separator();
                if (pin)
                {
                    ImGui::Text("ID: %p", pin->ID.AsPointer());
                    if (pin->Node)
                        ImGui::Text("Node: %p", pin->Node->ID.AsPointer());
                    else
                        ImGui::Text("Node: %s", "<none>");
                }
                else
                    ImGui::Text("Unknown pin: %p", contextPinId.AsPointer());

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Link Context Menu"))
            {
                auto link = FindLink(contextLinkId);

                ImGui::TextUnformatted("Link Context Menu");
                ImGui::Separator();
                if (link)
                {
                    ImGui::Text("ID: %p", link->ID.AsPointer());
                    ImGui::Text("From: %p", link->StartPinID.AsPointer());
                    ImGui::Text("To: %p", link->EndPinID.AsPointer());
                }
                else
                    ImGui::Text("Unknown link: %p", contextLinkId.AsPointer());
                ImGui::Separator();
                if (ImGui::MenuItem("Delete"))
                    ed::DeleteLink(contextLinkId);
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("Create New Node"))
            {
                auto newNodePostion = openPopupPosition;
                //ImGui::SetCursorScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());

                //auto drawList = ImGui::GetWindowDrawList();
                //drawList->AddCircleFilled(ImGui::GetMousePosOnOpeningCurrentPopup(), 10.0f, 0xFFFF00FF);

                Node* node = nullptr;
                if (ImGui::MenuItem("Value"))
                    node = SpawnValueNode("");
                ImGui::Separator();

                if (ImGui::MenuItem("Attribute"))
                    node = SpawnAttributeNode(0, "", NULL);
                ImGui::Separator();

                if (node)
                {
                    BuildNodes();

                    createNewNode = false;

                    ed::SetNodePosition(node->ID, newNodePostion);

                    if (auto startPin = newNodeLinkPin)
                    {
                        auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                        for (auto& pin : pins)
                        {
                            if (CanCreateLink(startPin, &pin))
                            {
                                auto endPin = &pin;
                                if (startPin->Kind == PinKind::Input)
                                    std::swap(startPin, endPin);

                                m_Links.emplace_back(Link(GetNextId(), startPin->ID, endPin->ID));
                                m_Links.back().Color = GetIconColor(startPin->Type);

                                break;
                            }
                        }
                    }
                }

                ImGui::EndPopup();
            }
            else
                createNewNode = false;
            ImGui::PopStyleVar();
            ed::Resume();
        # endif
        

            ed::End();

            auto editorMin = ImGui::GetItemRectMin();
            auto editorMax = ImGui::GetItemRectMax();

            if (m_ShowOrdinals)
            {
                int nodeCount = ed::GetNodeCount();
                std::vector<ed::NodeId> orderedNodeIds;
                orderedNodeIds.resize(static_cast<size_t>(nodeCount));
                ed::GetOrderedNodeIds(orderedNodeIds.data(), nodeCount);


                auto drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(editorMin, editorMax);

                int ordinal = 0;
                for (auto& nodeId : orderedNodeIds)
                {
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

        }
        
        xml_document<>                          doc;
        xml_node<>*                             root_node = nullptr;
        int                                     m_NextId = 1;
        const int                               m_PinIconSize = 24;
        vector<Node>                            m_Nodes;
        vector<Link>                            m_Links;
        Graph*                                  graph;
        vector<vector<int>>                     levels_x;
        ImTextureID                             m_HeaderBackground = nullptr;
        ImTextureID                             m_SaveIcon = nullptr;
        ImTextureID                             m_RestoreIcon = nullptr;
        const float                             m_TouchTime = 1.0f;
        map<ed::NodeId, float, NodeIdLess>      m_NodeTouchTime;
        bool                                    m_ShowOrdinals = false;
};


int Main(int argc, char** argv) {
    Example example((path + string("Blueprints")).c_str(), argc, argv);

    if(example.Create())
        return example.Run();

    return 0;
}