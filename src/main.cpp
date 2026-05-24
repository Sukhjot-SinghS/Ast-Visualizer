#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <tree_sitter/api.h>
#include "../json.hpp"
#include <cstdio>
#include <chrono>

using namespace std;
using json = nlohmann::json; 

extern "C" TSLanguage* tree_sitter_cpp();

// ==========================================
// 1. HELPER: Sanitize for DOT
// ==========================================
string escape_dot(string text) {
    string result;
    for (char c : text) {
        if (c == '"' || c == '\\') result += '\\';
        if (c == '\n') { result += "\\n"; continue; }
        result += c;
    }
    return result;
}

// ==========================================
// 2. JSON GENERATOR (For Web Frontend)
// ==========================================
json traverse_node_json(TSNode node, const string& source, int& id_counter, map<int,string>& error_map) {
    json j_node; 
    j_node["id"] = id_counter++;
    j_node["type"] = ts_node_type(node);

    if (ts_node_named_child_count(node) == 0) {
        uint32_t start_byte = ts_node_start_byte(node);
        uint32_t end_byte = ts_node_end_byte(node);
        j_node["text"] = source.substr(start_byte, end_byte - start_byte);
    }

    TSPoint start_point = ts_node_start_point(node);
    int current_line = start_point.row + 1;
    
    // Logic: If error exists, attach it and ERASE to prevent cascading
    if (error_map.find(current_line) != error_map.end()) {
        j_node["has_error"] = true;
        j_node["gcc_error"] = error_map[current_line];
        error_map.erase(current_line); 
    } else if (string(ts_node_type(node)) == "ERROR") {
        j_node["has_error"] = true;
        j_node["gcc_error"] = "Tree-sitter Syntax Error";
    } else {
        j_node["has_error"] = false;
    }

    j_node["children"] = json::array(); 
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            const char* field_name = ts_tree_cursor_current_field_name(&cursor);
            TSNode child = ts_tree_cursor_current_node(&cursor);
            if (ts_node_is_named(child)) {
                json child_json = traverse_node_json(child, source, id_counter, error_map);
                if (field_name) child_json["field_name"] = field_name;
                j_node["children"].push_back(child_json);
            }
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    return j_node; 
}

// ==========================================
// 3. DOT GENERATOR (With Map + Cascade Fix)
// ==========================================
int traverse_node_dot(TSNode node, const string& source, ofstream& out, int& id_counter, map<int,string> &error) {
    int current_id = id_counter++;
    string type = ts_node_type(node);
    string label = type;

    if (ts_node_named_child_count(node) == 0) {
        uint32_t start_byte = ts_node_start_byte(node);
        uint32_t end_byte = ts_node_end_byte(node);
        string text = source.substr(start_byte, end_byte - start_byte);
        label += "\\n'" + escape_dot(text) + "'"; 
    }
    TSPoint start_point = ts_node_start_point(node);
    int current_line = start_point.row + 1;

    // Node Coloring Logic
    string fillcolor = "\"#f0f0f0\""; 
    if (type == "function_definition") fillcolor = "\"#a1ef9b\"";
    else if (type == "identifier") fillcolor = "\"#9bdaef\"";
    else if (type == "ERROR") fillcolor = "\"#ff6b6b\"";
 
    // Cascade-proof logic: erase after use
    if (error.find(current_line) == error.end()) { 
        out << "    node" << current_id << " [label=\"" << label << "\", shape=box, style=\"filled,rounded,bold\", fillcolor=" << fillcolor << ", color=\"#444444\"];\n";
    } else {
        out << "    node" << current_id << " [label=\"" << label << "\\n[GCC " << escape_dot(error[current_line]) << "]\", shape=box, style=\"filled,rounded,bold\", fillcolor=\"#ff6b6b\", color=\"#444444\"];\n";
        error.erase(current_line);
    }
    
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    if (ts_tree_cursor_goto_first_child(&cursor)) {
        do {
            const char* field_name = ts_tree_cursor_current_field_name(&cursor);
            string edge_label = field_name ? field_name : "";
            TSNode child = ts_tree_cursor_current_node(&cursor);
            if (ts_node_is_named(child)) {
                int child_id = traverse_node_dot(child, source, out, id_counter, error);
                out << "    node" << current_id << " -> node" << child_id << "[label=\"" << edge_label << "\", fontcolor=\"#888888\", fontsize=8];\n";
            }
        } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    return current_id; 
}

// ==========================================
// 4. MAIN ROUTER
// ==========================================
int main(int argc, char** argv) {
    // Initialise chrono timer at the start
    auto start_time = std::chrono::high_resolution_clock::now();

    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <path_to_cpp_file> [--dot]\n";
        return 1;
    }

    ifstream file(argv[1]);
    if (!file.is_open()) return 1;
    stringstream buffer;
    buffer << file.rdbuf();
    string source_code = buffer.str();

    // 1. GCC Diagnostic Hook (Dynamic path)
    string command = "g++ -fsyntax-only " + string(argv[1]) + " 2> errors.txt";
    system(command.c_str());
    
    ifstream err_file("errors.txt");
    string line;
    regex err_regex(".*?:(\\d+):.*error:\\s*(.*)"); 
    smatch match;
    map<int,string> error_map;
    while (getline(err_file, line)) {
        if (regex_search(line, match, err_regex)) {
            error_map[stoi(match[1].str())] = match[2].str(); 
        }
    }
    remove("errors.txt");

    // 2. Parser Boot
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_cpp());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, source_code.c_str(), source_code.length());
    TSNode root_node = ts_tree_root_node(tree);
    int initial_id = 0;

    // 3. Router
    if (argc > 2 && string(argv[2]) == "--dot") {
        string base_name = string(argv[1]);
        base_name = base_name.substr(base_name.find_last_of("/\\") + 1);
        base_name.erase(base_name.find(".cpp"));
        
        ofstream out_file(base_name + "_tree.dot");
        out_file << "digraph AST {\n    node [fontname=\"Helvetica\", fontsize=10];\n"; 
        traverse_node_dot(root_node, source_code, out_file, initial_id, error_map);
        out_file << "}\n"; 
        out_file.close();        
        system(("dot -Tsvg " + base_name + "_tree.dot -o " + base_name + "_viz.svg").c_str());
        remove((base_name + "_tree.dot").c_str());
        cout << "Generated: " << base_name + "_viz.svg" << endl;
    } else {
   
        json ast_json = traverse_node_json(root_node, source_code, initial_id, error_map);
        
        // Robust filename extraction (works no matter what folder the file is in!)
        string base_name = string(argv[1]);
        size_t slash_pos = base_name.find_last_of("/\\");
        if (slash_pos != string::npos) {
            base_name = base_name.substr(slash_pos + 1); // Isolates "clean.cpp"
        }
        base_name.erase(base_name.find(".cpp")); // Isolates "clean"
        
        // Save directly to the frontend folder
        string out_path = "frontend/" + base_name + "_code.json";
        ofstream out_file(out_path.c_str());
        out_file << ast_json.dump(4) << endl; 
        out_file.close();
        
        cout << "Generated: " << out_path << endl;
    
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    // Final chrono timing
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_time - start_time;
    cout << "Total time: " << elapsed_seconds.count() << "s" << endl;

    return 0;
}