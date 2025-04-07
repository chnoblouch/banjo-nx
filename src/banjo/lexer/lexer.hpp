#ifndef LEXER_H
#define LEXER_H

#include "banjo/lexer/buffered_reader.hpp"
#include "banjo/lexer/token.hpp"

#include <istream>
#include <vector>

namespace banjo {

namespace lang {

class Lexer {

public:
    enum class Mode {
        COMPILATION,
        FORMATTING,
    };

private:
    BufferedReader reader;
    Mode mode;

    bool current_line_empty = true;
    bool previous_line_empty = false;

    bool completion_enabled = false;
    TextPosition completion_point;
    bool completion_token_inserted = false;

    std::vector<Token> tokens;

    std::string token_builder;
    TextPosition start_position;

public:
    Lexer(std::istream &input_stream, Mode mode = Mode::COMPILATION);
    void enable_completion(TextPosition completion_point);
    std::vector<Token> tokenize();

private:
    void read_token();
    void read_newline();
    void read_whitespace();
    void read_identifier();
    void read_number();
    void read_character();
    void read_string();
    void skip_comment();
    void read_punctuation();

    void finish_token(TokenType type);
    void finish_line();
    void try_insert_completion_token();

    bool is_whitespace_char(int c);
    bool is_identifier_char(int c);
    bool is_number_char(int c);
    bool read_if(int c);
};

} // namespace lang

} // namespace banjo

#endif
