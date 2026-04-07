#include "../../include/parser/parser.h"
#include <algorithm>
#include <cctype>
#include <iostream>

namespace flexql {

// ==================== LEXER ====================

Lexer::Lexer(const std::string& input) : input_(input), data_(input_.data()), len_(input_.size()), pos_(0), has_peeked_(false) {}

Lexer::Lexer(const char* data, size_t len) : data_(data), len_(len), pos_(0), has_peeked_(false) {}

void Lexer::skip_whitespace() {
    while (pos_ < len_ && std::isspace(data_[pos_])) {
        pos_++;
    }
}

Token Lexer::read_string() {
    char quote = data_[pos_++]; // skip opening quote
    std::string value;
    value.reserve(32); // Pre-allocate for typical string length
    while (pos_ < len_ && data_[pos_] != quote) {
        if (data_[pos_] == '\\' && pos_ + 1 < len_) {
            pos_++;
            value += data_[pos_];
        } else {
            value += data_[pos_];
        }
        pos_++;
    }
    if (pos_ < len_) pos_++; // skip closing quote
    return Token(TokenType::STRING_LITERAL, std::move(value));
}

Token Lexer::read_number() {
    std::string value;
    value.reserve(16); // Pre-allocate for typical number length
    bool has_dot = false;
    bool has_minus = false;
    
    if (pos_ < len_ && data_[pos_] == '-') {
        value += '-';
        pos_++;
        has_minus = true;
    }
    
    while (pos_ < len_ && (std::isdigit(data_[pos_]) || data_[pos_] == '.')) {
        if (data_[pos_] == '.') {
            if (has_dot) break;
            has_dot = true;
        }
        value += data_[pos_++];
    }
    
    if (has_dot) {
        return Token(TokenType::DECIMAL_LITERAL, std::move(value));
    }
    return Token(TokenType::INTEGER_LITERAL, std::move(value));
}

Token Lexer::read_identifier_or_keyword() {
    size_t start = pos_;
    while (pos_ < len_ && (std::isalnum(data_[pos_]) || data_[pos_] == '_')) {
        pos_++;
    }
    
    std::string value(data_ + start, pos_ - start);
    
    // Check for keyword using length-first fast path
    size_t len = value.size();
    char c0 = (len > 0) ? (value[0] | 0x20) : 0; // lowercase first char
    
    switch (len) {
        case 2:
            if ((c0 == 'b') && ((value[1] | 0x20) == 'y')) return Token(TokenType::BY, value);
            if ((c0 == 'o') && ((value[1] | 0x20) == 'n')) return Token(TokenType::ON, value);
            if ((c0 == 'o') && ((value[1] | 0x20) == 'r')) return Token(TokenType::OR, value);
            if ((c0 == 'i') && ((value[1] | 0x20) == 'f')) return Token(TokenType::IF, value);
            break;
        case 3:
            if ((c0 == 'i') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 't')) return Token(TokenType::INT_TYPE, value);
            if ((c0 == 'a') && ((value[1] | 0x20) == 's') && ((value[2] | 0x20) == 'c')) return Token(TokenType::ASC, value);
            if ((c0 == 'a') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 'd')) return Token(TokenType::AND, value);
            if ((c0 == 'k') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 'y')) return Token(TokenType::KEY, value);
            if ((c0 == 'n') && ((value[1] | 0x20) == 'o') && ((value[2] | 0x20) == 't')) return Token(TokenType::NOT, value);
            break;
        case 4:
            if ((c0 == 'f') && ((value[1] | 0x20) == 'r') && ((value[2] | 0x20) == 'o') && ((value[3] | 0x20) == 'm')) return Token(TokenType::FROM, value);
            if ((c0 == 'j') && ((value[1] | 0x20) == 'o') && ((value[2] | 0x20) == 'i') && ((value[3] | 0x20) == 'n')) return Token(TokenType::JOIN, value);
            if ((c0 == 'd') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 's') && ((value[3] | 0x20) == 'c')) return Token(TokenType::DESC, value);
            if ((c0 == 'n') && ((value[1] | 0x20) == 'u') && ((value[2] | 0x20) == 'l') && ((value[3] | 0x20) == 'l')) return Token(TokenType::NULL_TOKEN, value);
            if ((c0 == 'i') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 't') && ((value[3] | 0x20) == 'o')) return Token(TokenType::INTO, value);
            if ((c0 == 't') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 'x') && ((value[3] | 0x20) == 't')) return Token(TokenType::TEXT_TYPE, value);
            break;
        case 5:
            if ((c0 == 't') && ((value[1] | 0x20) == 'a') && ((value[2] | 0x20) == 'b') && ((value[3] | 0x20) == 'l') && ((value[4] | 0x20) == 'e')) return Token(TokenType::TABLE, value);
            if ((c0 == 'w') && ((value[1] | 0x20) == 'h') && ((value[2] | 0x20) == 'e') && ((value[3] | 0x20) == 'r') && ((value[4] | 0x20) == 'e')) return Token(TokenType::WHERE, value);
            if ((c0 == 'o') && ((value[1] | 0x20) == 'r') && ((value[2] | 0x20) == 'd') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 'r')) return Token(TokenType::ORDER, value);
            if ((c0 == 'f') && ((value[1] | 0x20) == 'l') && ((value[2] | 0x20) == 'o') && ((value[3] | 0x20) == 'a') && ((value[4] | 0x20) == 't')) return Token(TokenType::DECIMAL_TYPE, value);
            if ((c0 == 'i') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 'n') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 'r')) return Token(TokenType::INNER, value);
            break;
        case 6:
            if ((c0 == 'c') && ((value[1] | 0x20) == 'r') && ((value[2] | 0x20) == 'e') && ((value[3] | 0x20) == 'a') && ((value[4] | 0x20) == 't') && ((value[5] | 0x20) == 'e')) return Token(TokenType::CREATE, value);
            if ((c0 == 'i') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 's') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 'r') && ((value[5] | 0x20) == 't')) return Token(TokenType::INSERT, value);
            if ((c0 == 's') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 'l') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 'c') && ((value[5] | 0x20) == 't')) return Token(TokenType::SELECT, value);
            if ((c0 == 'd') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 'l') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 't') && ((value[5] | 0x20) == 'e')) return Token(TokenType::DELETE, value);
            if ((c0 == 'd') && ((value[1] | 0x20) == 'o') && ((value[2] | 0x20) == 'u') && ((value[3] | 0x20) == 'b') && ((value[4] | 0x20) == 'l') && ((value[5] | 0x20) == 'e')) return Token(TokenType::DECIMAL_TYPE, value);
            if ((c0 == 'e') && ((value[1] | 0x20) == 'x') && ((value[2] | 0x20) == 'i') && ((value[3] | 0x20) == 's') && ((value[4] | 0x20) == 't') && ((value[5] | 0x20) == 's')) return Token(TokenType::EXISTS, value);
            if ((c0 == 'v') && ((value[1] | 0x20) == 'a') && ((value[2] | 0x20) == 'l') && ((value[3] | 0x20) == 'u') && ((value[4] | 0x20) == 'e') && ((value[5] | 0x20) == 's')) return Token(TokenType::VALUES, value);
            break;
        case 7:
            if ((c0 == 'p') && ((value[1] | 0x20) == 'r') && ((value[2] | 0x20) == 'i') && ((value[3] | 0x20) == 'm') && ((value[4] | 0x20) == 'a') && ((value[5] | 0x20) == 'r') && ((value[6] | 0x20) == 'y')) return Token(TokenType::PRIMARY, value);
            if ((c0 == 'i') && ((value[1] | 0x20) == 'n') && ((value[2] | 0x20) == 't') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 'g') && ((value[5] | 0x20) == 'e') && ((value[6] | 0x20) == 'r')) return Token(TokenType::INT_TYPE, value);
            if ((c0 == 'v') && ((value[1] | 0x20) == 'a') && ((value[2] | 0x20) == 'r') && ((value[3] | 0x20) == 'c') && ((value[4] | 0x20) == 'h') && ((value[5] | 0x20) == 'a') && ((value[6] | 0x20) == 'r')) return Token(TokenType::VARCHAR_TYPE, value);
            if ((c0 == 'd') && ((value[1] | 0x20) == 'e') && ((value[2] | 0x20) == 'c') && ((value[3] | 0x20) == 'i') && ((value[4] | 0x20) == 'm') && ((value[5] | 0x20) == 'a') && ((value[6] | 0x20) == 'l')) return Token(TokenType::DECIMAL_TYPE, value);
            break;
        case 8:
            if ((c0 == 'd') && ((value[1] | 0x20) == 'a') && ((value[2] | 0x20) == 't') && ((value[3] | 0x20) == 'e') && ((value[4] | 0x20) == 't') && ((value[5] | 0x20) == 'i') && ((value[6] | 0x20) == 'm') && ((value[7] | 0x20) == 'e')) return Token(TokenType::DATETIME_TYPE, value);
            break;
    }
    
    return Token(TokenType::IDENTIFIER, value);
}

// Static strings for single-char tokens to avoid allocations
static const std::string LPAREN_STR("(");
static const std::string RPAREN_STR(")");
static const std::string COMMA_STR(",");
static const std::string SEMICOLON_STR(";");
static const std::string STAR_STR("*");
static const std::string DOT_STR(".");
static const std::string EQUALS_STR("=");
static const std::string GREATER_STR(">");
static const std::string LESS_STR("<");
static const std::string GREATER_EQ_STR(">=");
static const std::string LESS_EQ_STR("<=");
static const std::string EMPTY_STR("");

Token Lexer::next_token() {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }
    
    skip_whitespace();
    
    if (pos_ >= len_) {
        return Token(TokenType::END_OF_INPUT, EMPTY_STR);
    }
    
    char c = data_[pos_];
    
    // Fast path for single-char tokens - use static strings to avoid allocation
    switch (c) {
        case '(': pos_++; return Token(TokenType::LPAREN, LPAREN_STR);
        case ')': pos_++; return Token(TokenType::RPAREN, RPAREN_STR);
        case ',': pos_++; return Token(TokenType::COMMA, COMMA_STR);
        case ';': pos_++; return Token(TokenType::SEMICOLON, SEMICOLON_STR);
        case '*': pos_++; return Token(TokenType::STAR, STAR_STR);
        case '.': pos_++; return Token(TokenType::DOT, DOT_STR);
        case '=': pos_++; return Token(TokenType::EQUALS, EQUALS_STR);
        case '>':
            pos_++;
            if (pos_ < len_ && data_[pos_] == '=') {
                pos_++;
                return Token(TokenType::GREATER_EQ, GREATER_EQ_STR);
            }
            return Token(TokenType::GREATER, GREATER_STR);
        case '<':
            pos_++;
            if (pos_ < len_ && data_[pos_] == '=') {
                pos_++;
                return Token(TokenType::LESS_EQ, LESS_EQ_STR);
            }
            return Token(TokenType::LESS, LESS_STR);
        case '\'':
        case '"':
            return read_string();
    }
    
    if (std::isdigit(c) || (c == '-' && pos_ + 1 < len_ && std::isdigit(data_[pos_ + 1]))) {
        return read_number();
    }
    
    if (std::isalpha(c) || c == '_') {
        return read_identifier_or_keyword();
    }
    
    pos_++;
    return Token(TokenType::END_OF_INPUT, EMPTY_STR);
}

Token Lexer::peek_token() {
    if (!has_peeked_) {
        peeked_ = next_token();
        has_peeked_ = true;
    }
    return peeked_;
}

// ==================== PARSER ====================

Parser::Parser(const std::string& sql) : lexer_(sql) {
    advance();
}

Parser::Parser(const char* data, size_t len) : lexer_(data, len) {
    advance();
}

void Parser::advance() {
    current_ = lexer_.next_token();
}

bool Parser::check(TokenType type) {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (current_.type == type) {
        advance();
        return true;
    }
    return false;
}

void Parser::expect(TokenType type) {
    if (!match(type)) {
        throw std::runtime_error("Unexpected token: '" + current_.value + "'");
    }
}

Statement Parser::parse() {
    if (check(TokenType::CREATE)) {
        return parse_create();
    } else if (check(TokenType::INSERT)) {
        return parse_insert();
    } else if (check(TokenType::SELECT)) {
        return parse_select();
    } else if (check(TokenType::DELETE)) {
        return parse_delete();
    }
    throw std::runtime_error("Unsupported SQL statement starting with: " + current_.value);
}

Statement Parser::parse_create() {
    Statement stmt;
    stmt.type = StmtType::CREATE_TABLE;
    
    expect(TokenType::CREATE);
    expect(TokenType::TABLE);
    
    // IF NOT EXISTS
    if (check(TokenType::IF)) {
        advance(); // IF
        expect(TokenType::NOT);
        expect(TokenType::EXISTS);
        stmt.create_table.if_not_exists = true;
    }
    
    // Table name
    stmt.create_table.table_name = current_.value;
    advance();
    
    expect(TokenType::LPAREN);
    
    // Columns
    while (!check(TokenType::RPAREN) && !check(TokenType::END_OF_INPUT)) {
        stmt.create_table.columns.push_back(parse_column_def());
        if (!check(TokenType::RPAREN)) {
            expect(TokenType::COMMA);
        }
    }
    
    expect(TokenType::RPAREN);
    match(TokenType::SEMICOLON);
    
    return stmt;
}

ColumnDef Parser::parse_column_def() {
    ColumnDef col;
    col.name = current_.value;
    advance();
    
    // Type
    if (check(TokenType::INT_TYPE)) {
        col.type = ColumnType::INT;
        advance();
    } else if (check(TokenType::DECIMAL_TYPE)) {
        col.type = ColumnType::DECIMAL;
        advance();
    } else if (check(TokenType::VARCHAR_TYPE)) {
        col.type = ColumnType::VARCHAR;
        advance();
        // Optional (length)
        if (match(TokenType::LPAREN)) {
            col.max_length = (uint16_t)std::stoi(current_.value);
            advance();
            expect(TokenType::RPAREN);
        }
    } else if (check(TokenType::DATETIME_TYPE)) {
        col.type = ColumnType::DATETIME;
        advance();
    } else if (check(TokenType::TEXT_TYPE)) {
        col.type = ColumnType::VARCHAR;
        col.max_length = 255;
        advance();
    } else {
        throw std::runtime_error("Unknown column type: " + current_.value);
    }
    
    // Optional constraints: PRIMARY KEY, NOT NULL
    while (check(TokenType::PRIMARY) || check(TokenType::NOT)) {
        if (match(TokenType::PRIMARY)) {
            expect(TokenType::KEY);
            col.is_primary_key = true;
        } else if (match(TokenType::NOT)) {
            expect(TokenType::NULL_TOKEN);
            col.not_null = true;
        }
    }
    
    return col;
}

Statement Parser::parse_insert() {
    Statement stmt;
    stmt.type = StmtType::INSERT;
    
    expect(TokenType::INSERT);
    expect(TokenType::INTO);
    
    stmt.insert.table_name = current_.value;
    advance();
    
    expect(TokenType::VALUES);
    
    // Pre-allocate for large batch sizes (50K typical)
    stmt.insert.value_rows.reserve(65536);
    
    // Parse value tuples (batch support)
    do {
        stmt.insert.value_rows.push_back(parse_value_tuple());
    } while (match(TokenType::COMMA));
    
    match(TokenType::SEMICOLON);
    
    return stmt;
}

std::vector<std::string> Parser::parse_value_tuple() {
    std::vector<std::string> values;
    values.reserve(8); // Pre-allocate for typical row size
    
    expect(TokenType::LPAREN);
    
    while (!check(TokenType::RPAREN) && !check(TokenType::END_OF_INPUT)) {
        if (check(TokenType::STRING_LITERAL)) {
            // Mark as string by prefixing with a special marker
            std::string str_val;
            str_val.reserve(current_.value.size() + 2);
            str_val = '\'';
            str_val += current_.value;
            str_val += '\'';
            values.push_back(std::move(str_val));
            advance();
        } else if (check(TokenType::NULL_TOKEN)) {
            values.emplace_back("NULL");
            advance();
        } else {
            // Handle negative numbers
            if (check(TokenType::INTEGER_LITERAL) || check(TokenType::DECIMAL_LITERAL) || check(TokenType::IDENTIFIER)) {
                values.push_back(std::move(current_.value));
                advance();
            } else {
                values.push_back(std::move(current_.value));
                advance();
            }
        }
        
        if (!check(TokenType::RPAREN)) {
            expect(TokenType::COMMA);
        }
    }
    
    expect(TokenType::RPAREN);
    
    return values;
}

Condition Parser::parse_condition() {
    Condition cond;
    
    // Column name (may be table.column)
    cond.column = current_.value;
    advance();
    
    // Check for dotted name
    if (match(TokenType::DOT)) {
        cond.column += "." + current_.value;
        advance();
    }
    
    // Operator
    if (match(TokenType::EQUALS)) {
        cond.op = CompOp::EQ;
    } else if (match(TokenType::GREATER)) {
        cond.op = CompOp::GT;
    } else if (match(TokenType::LESS)) {
        cond.op = CompOp::LT;
    } else if (match(TokenType::GREATER_EQ)) {
        cond.op = CompOp::GE;
    } else if (match(TokenType::LESS_EQ)) {
        cond.op = CompOp::LE;
    } else {
        throw std::runtime_error("Expected comparison operator, got: " + current_.value);
    }
    
    // Value
    if (check(TokenType::STRING_LITERAL)) {
        cond.value = current_.value;
        cond.value_is_string = true;
        advance();
    } else if (check(TokenType::INTEGER_LITERAL) || check(TokenType::DECIMAL_LITERAL)) {
        cond.value = current_.value;
        cond.value_is_string = false;
        advance();
    } else if (check(TokenType::NULL_TOKEN)) {
        cond.value = "NULL";
        cond.value_is_string = false;
        advance();
    } else {
        // Could be an identifier or number
        cond.value = current_.value;
        cond.value_is_string = false;
        advance();
        // Check for dotted
        if (match(TokenType::DOT)) {
            cond.value += "." + current_.value;
            advance();
        }
    }
    
    return cond;
}

Statement Parser::parse_select() {
    Statement stmt;
    stmt.type = StmtType::SELECT;
    
    expect(TokenType::SELECT);
    
    // Columns
    if (match(TokenType::STAR)) {
        stmt.select.select_all = true;
    } else {
        // Column list
        do {
            std::string col_name = current_.value;
            advance();
            if (match(TokenType::DOT)) {
                col_name += "." + current_.value;
                advance();
            }
            stmt.select.columns.push_back(col_name);
        } while (match(TokenType::COMMA));
    }
    
    // FROM
    expect(TokenType::FROM);
    stmt.select.table_name = current_.value;
    advance();
    
    // INNER JOIN
    if (check(TokenType::INNER)) {
        advance(); // INNER
        expect(TokenType::JOIN);
        
        stmt.select.has_join = true;
        stmt.select.join.join_table = current_.value;
        advance();
        
        expect(TokenType::ON);
        
        // Join condition: t1.col = t2.col
        stmt.select.join.left_column = current_.value;
        advance();
        if (match(TokenType::DOT)) {
            stmt.select.join.left_column += "." + current_.value;
            advance();
        }
        
        expect(TokenType::EQUALS);
        
        stmt.select.join.right_column = current_.value;
        advance();
        if (match(TokenType::DOT)) {
            stmt.select.join.right_column += "." + current_.value;
            advance();
        }
    }
    
    // WHERE
    if (check(TokenType::WHERE)) {
        advance();
        stmt.select.has_where = true;
        stmt.select.where_cond = parse_condition();
    }
    
    // ORDER BY
    if (check(TokenType::ORDER)) {
        advance(); // ORDER
        expect(TokenType::BY);
        stmt.select.has_order_by = true;
        stmt.select.order_by.column = current_.value;
        advance();
        if (match(TokenType::DOT)) {
            stmt.select.order_by.column += "." + current_.value;
            advance();
        }
        if (match(TokenType::DESC)) {
            stmt.select.order_by.direction = OrderDir::DESC;
        } else {
            match(TokenType::ASC); // optional
            stmt.select.order_by.direction = OrderDir::ASC;
        }
    }
    
    match(TokenType::SEMICOLON);
    
    return stmt;
}

Statement Parser::parse_delete() {
    Statement stmt;
    stmt.type = StmtType::DELETE_FROM;
    
    expect(TokenType::DELETE);
    expect(TokenType::FROM);
    
    stmt.delete_from.table_name = current_.value;
    advance();
    
    if (check(TokenType::WHERE)) {
        advance();
        stmt.delete_from.has_where = true;
        stmt.delete_from.where_cond = parse_condition();
    }
    
    match(TokenType::SEMICOLON);
    
    return stmt;
}

} // namespace flexql
