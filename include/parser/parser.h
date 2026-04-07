#ifndef FLEXQL_PARSER_PARSER_H
#define FLEXQL_PARSER_PARSER_H

#include "ast.h"
#include <string>
#include <vector>
#include <stdexcept>

namespace flexql {

enum class TokenType {
    // Keywords
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, WHERE,
    INNER, JOIN, ON, AND, OR, ORDER, BY, ASC, DESC,
    DELETE, IF, NOT, EXISTS, PRIMARY, KEY, NULL_TOKEN,
    // Types
    INT_TYPE, DECIMAL_TYPE, VARCHAR_TYPE, DATETIME_TYPE, TEXT_TYPE,
    // Literals
    INTEGER_LITERAL, DECIMAL_LITERAL, STRING_LITERAL,
    // Operators
    EQUALS, GREATER, LESS, GREATER_EQ, LESS_EQ,
    // Punctuation
    LPAREN, RPAREN, COMMA, SEMICOLON, STAR, DOT,
    // Other
    IDENTIFIER,
    END_OF_INPUT
};

struct Token {
    TokenType type;
    std::string value;
    Token() : type(TokenType::END_OF_INPUT) {}
    Token(TokenType t, const std::string& v) : type(t), value(v) {}
    Token(TokenType t, std::string&& v) : type(t), value(std::move(v)) {}
};

class Lexer {
public:
    Lexer(const std::string& input);
    Lexer(const char* data, size_t len);  // Allow non-copying version
    Token next_token();
    Token peek_token();
    
private:
    std::string input_;  // Only used if copy is needed
    const char* data_;   // Points to input data
    size_t len_;
    size_t pos_;
    Token peeked_;
    bool has_peeked_;
    
    void skip_whitespace();
    Token read_string();
    Token read_number();
    Token read_identifier_or_keyword();
};

class Parser {
public:
    Parser(const std::string& sql);
    Parser(const char* data, size_t len);  // Non-copying version
    Statement parse();
    
private:
    Lexer lexer_;
    Token current_;
    
    void advance();
    void expect(TokenType type);
    bool match(TokenType type);
    bool check(TokenType type);
    
    Statement parse_create();
    Statement parse_insert();
    Statement parse_select();
    Statement parse_delete();
    
    ColumnDef parse_column_def();
    Condition parse_condition();
    std::vector<std::string> parse_value_tuple();
};

} // namespace flexql

#endif
