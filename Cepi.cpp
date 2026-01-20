#include <bitset>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>

using namespace std;
const string SOURCE_DATA = "bigtext.txt";
const size_t CHECKPOINT_STEP = 1000000;

uint16_t compress_float(float value) {
    uint32_t raw_bits;
    memcpy(&raw_bits, &value, sizeof(raw_bits));
    uint32_t sign_flag = (raw_bits >> 16) & 0x8000;
    uint32_t fraction = raw_bits & 0x007FFFFF;
    int32_t exponent = ((raw_bits >> 23) & 0xFF) - 127 + 15;
    if (exponent <= 0) {
        return sign_flag;
    }
    if (exponent >= 31) {
        return sign_flag | 0x7C00;
    }
    return sign_flag | (exponent << 10) | (fraction >> 13);
}
void add_unicode_char(string& target, uint32_t code_point) {
    if (code_point <= 0x7F) {
        target.push_back((char)code_point);
    }
    else if (code_point <= 0x7FF) {
        target.push_back((char)(0xC0 | ((code_point >> 6) & 0x1F)));
        target.push_back((char)(0x80 | (code_point & 0x3F)));
    }
    else if (code_point <= 0xFFFF) {
        target.push_back((char)(0xE0 | ((code_point >> 12) & 0x0F)));
        target.push_back((char)(0x80 | ((code_point >> 6) & 0x3F)));
        target.push_back((char)(0x80 | (code_point & 0x3F)));
    }
    else {
        target.push_back((char)(0xF0 | ((code_point >> 18) & 0x07)));
        target.push_back((char)(0x80 | ((code_point >> 12) & 0x3F)));
        target.push_back((char)(0x80 | ((code_point >> 6) & 0x3F)));
        target.push_back((char)(0x80 | (code_point & 0x3F)));
    }
}
void iterate_unicode_chars(const string& input_str, const function<void(uint32_t)>& callback) {
    const unsigned char* data_ptr = (const unsigned char*)input_str.data();
    size_t position = 0;
    size_t str_len = input_str.size();
    while (position < str_len) {
        unsigned char first_byte = data_ptr[position];
        uint32_t char_code = 0;

        if (first_byte < 0x80) {
            char_code = first_byte;
            position += 1;
        }
        else if ((first_byte >> 5) == 0x6) {
            char_code = ((data_ptr[position] & 0x1F) << 6) | (data_ptr[position + 1] & 0x3F);
            position += 2;
        }
        else if ((first_byte >> 4) == 0xE) {
            char_code = ((data_ptr[position] & 0x0F) << 12) |
                ((data_ptr[position + 1] & 0x3F) << 6) |
                (data_ptr[position + 2] & 0x3F);
            position += 3;
        }
        else if ((first_byte >> 3) == 0x1E) {
            char_code = ((data_ptr[position] & 0x07) << 18) |
                ((data_ptr[position + 1] & 0x3F) << 12) |
                ((data_ptr[position + 2] & 0x3F) << 6) |
                (data_ptr[position + 3] & 0x3F);
            position += 4;
        }
        else {
            position++;
            continue;
        }
        callback(char_code);
    }
}
string convert_to_unicode_escape(const string& utf8_input) {
    ostringstream result_stream;
    result_stream << hex << setfill('0');
    iterate_unicode_chars(utf8_input, [&](uint32_t char_code) {
        if (char_code <= 0xFFFF) {
            result_stream << "\\u" << setw(4) << (int)char_code;
        }
        else {
            uint32_t adjusted_value = char_code - 0x10000;
            uint16_t high_surrogate = 0xD800 | ((adjusted_value >> 10) & 0x3FF);
            uint16_t low_surrogate = 0xDC00 | (adjusted_value & 0x3FF);
            result_stream << "\\u" << setw(4) << (int)high_surrogate
                << "\\u" << setw(4) << (int)low_surrogate;
        }
        });
    result_stream << dec;
    return result_stream.str();
}
void store_count_data(const string& filename,
    const unordered_map<string, unordered_map<string, uint64_t>>& model_data)
{
    ofstream output_stream(filename, ios::binary);
    output_stream << "{\n";
    bool is_first_context = true;
    for (auto& context_entry : model_data) {
        if (!is_first_context) {
            output_stream << ",\n";
        }
        is_first_context = false;
        output_stream << "\"" << convert_to_unicode_escape(context_entry.first) << "\": {";
        bool is_first_symbol = true;
        for (auto& symbol_entry : context_entry.second) {
            if (!is_first_symbol) {
                output_stream << ", ";
            }
            is_first_symbol = false;
            output_stream << "\"" << convert_to_unicode_escape(symbol_entry.first)
                << "\": " << symbol_entry.second;
        }
        output_stream << "}";
    }
    output_stream << "\n}\n";
}
void store_final_model(const string& filename,
    const unordered_map<string, unordered_map<string, uint64_t>>& model_data)
{
    ofstream output_stream(filename, ios::binary);
    output_stream << "{\n";
    bool is_first_context = true;
    for (auto& context_entry : model_data) {
        if (!is_first_context) {
            output_stream << ",\n";
        }
        is_first_context = false;
        output_stream << "\"" << convert_to_unicode_escape(context_entry.first) << "\": {";
        uint64_t context_total = 0;
        for (auto& symbol_entry : context_entry.second) {
            context_total += symbol_entry.second;
        }
        bool is_first_symbol = true;
        for (auto& symbol_entry : context_entry.second) {
            if (!is_first_symbol) {
                output_stream << ", ";
            }
            is_first_symbol = false;

            float probability = context_total ? (float)symbol_entry.second / context_total : 0.0f;
            uint16_t compressed_prob = compress_float(probability);

            output_stream << "\"" << convert_to_unicode_escape(symbol_entry.first)
                << "\": " << compressed_prob;
        }
        output_stream << "}";
    }
    output_stream << "\n}\n";
}
string normalize_character(uint32_t char_code) {
    if (char_code >= U'À' && char_code <= U'ß') {
        char_code += 32;
    }
    else if (char_code == U'¨') {
        char_code = U'¸';
    }
    bool valid_char = ((char_code >= U'à' && char_code <= U'ÿ') ||
        char_code == U'¸' ||
        char_code == U'.' ||
        char_code == U',' ||
        char_code == U' ' ||
        char_code == U'!' ||
        char_code == U'?');
    if (!valid_char) {
        return "";
    }
    string result;
    add_unicode_char(result, char_code);
    return result;
}
void analyze_patterns(int n, bool continue_mode) {
    string progress_tracker = "knowledge_" + to_string(n) + ".progress";
    string temp_output = "knowledge_" + to_string(n) + "_temp.json";
    string final_output = "knowledge_" + to_string(n) + ".json";
    unordered_map<string, unordered_map<string, uint64_t>> pattern_counts;
    ifstream input_file(SOURCE_DATA, ios::binary);
    if (!input_file) {
        cerr << "Îøèáêà îòêðûòèÿ ôàéëà: " << SOURCE_DATA << "\n";
        return;
    }
    size_t bytes_processed = 0;
    if (continue_mode) {
        ifstream progress_file(progress_tracker);
        if (progress_file) {
            progress_file >> bytes_processed;
        }
        input_file.seekg(bytes_processed, ios::beg);
        cout << "[ÏÐÎÄÎËÆÅÍÈÅ] Ñìåùåíèå: " << bytes_processed << " áàéò\n";
    }
    deque<string> history_buffer;
    size_t total_bytes = bytes_processed;
    size_t char_count = 0;
    unsigned char current_byte;
    while (input_file.read((char*)&current_byte, 1)) {
        uint32_t char_code = 0;
        if (current_byte < 0x80) {
            char_code = current_byte;
        }
        else if ((current_byte >> 5) == 0x6) {
            unsigned char next_byte;
            if (!input_file.read((char*)&next_byte, 1)) break;
            char_code = ((current_byte & 0x1F) << 6) | (next_byte & 0x3F);
            total_bytes++;
        }
        else {
            unsigned char skip_byte;
            input_file.read((char*)&skip_byte, 1);
            total_bytes++;
            continue;
        }
        total_bytes++;
        string current_symbol = normalize_character(char_code);
        if (current_symbol.empty()) continue;
        if (history_buffer.size() < (size_t)n) {
            history_buffer.push_back(current_symbol);
            continue;
        }
        string context;
        for (auto& symbol : history_buffer) {
            context += symbol;
        }
        pattern_counts[context][current_symbol]++;
        history_buffer.pop_front();
        history_buffer.push_back(current_symbol);
        char_count++;
        if (char_count % CHECKPOINT_STEP == 0) {
            cout << "[ÑÎÕÐÀÍÅÍÈÅ] Ñèìâîëîâ îáðàáîòàíî: " << char_count << "\n";
            store_count_data(temp_output, pattern_counts);
            ofstream progress_file(progress_tracker);
            progress_file << total_bytes;
        }
    }
    cout << "[ÇÀÂÅÐØÅÍÈÅ] Ïîðÿäîê: " << n << ", âñåãî ñèìâîëîâ: " << char_count << "\n";
    store_final_model(final_output, pattern_counts);
    ofstream progress_file(progress_tracker);
    progress_file << total_bytes;
}
int program_main(int arg_count, char* arg_values[]) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int max_order = 0;
    bool resume_flag = false;
    for (int i = 1; i < arg_count; i++) {
        string current_arg = arg_values[i];

        if (current_arg == "--n" && i + 1 < arg_count) {
            max_order = stoi(arg_values[++i]);
        }
        else if (current_arg == "--continue") {
            resume_flag = true;
        }
    }

    if (max_order < 1 || max_order > 16) {
        cerr << "Èñïîëüçîâàíèå: chains --n <1-16> [--continue]\n";
        cerr << "  --n <range>   ìàêñèìàëüíûé ïîðÿäîê àíàëèçà (1-16)\n";
        cerr << "  --continue    ïðîäîëæèòü ñ ïîñëåäíåé òî÷êè ñîõðàíåíèÿ\n";
        return 1;
    }
    for (int current_order = 1; current_order <= max_order; current_order++) {
        cout << "=== Àíàëèç ïîðÿäêà " << current_order << " ===\n";
        analyze_patterns(current_order, resume_flag);
    }
    cout << "Çàâåðøåíî óñïåøíî.\n";
    return 0;
}
int main(int argc, char* argv[]) {
    return program_main(argc, argv);
}