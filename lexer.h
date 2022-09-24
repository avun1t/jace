#ifndef __LEXER_H_
#define __LEXER_H_

enum tokenType {
	TokenType_IDENTIFIED,

	//Literals
	TokenType_DIGIT,
	TokenType_FLOAT,
	TokenType_STRING,
	TokenType_BOOL,

	//Operators
	TokenType_PLUS,
	TokenType_MINUS,
	TokenType_TIMES,
	TokenType_SLASH,
	TokenType_PERIOD,
	TokenType_EQL,
	TokenType_NOT,
	TokenType_LSS,
	TokenType_GTR,
	TokenType_LEQ,
	TokenType_GEQ,

	//Seperators
	TokenType_LPAREN,
	TokenType_RPAREN,
	TokenType_LBRACK,
	TokenType_RBRACK,
	TokenType_LBRACE,
	TokenType_RBRACE,
	TokenType_SEMICOLON,
	TokenType_COMMA,
	TokenType_BECOMES,
	
	//Keywords
	TokenType_IF,
	TokenType_WHILE,
	TokenType_FOR,
	TokenType_RETURN,
	TokenType_DO,
	TokenType_NEW,
	TokenType_DELETE,
	TokenType_NULL,

	//Unique
	TokenType_UNKNOWN,
	TokenType_EOF
};

struct {
	TokenType type;
	char *contents;
	int length;
} token_t;

struct {
	token_t *tokens;
	int count;
} tokenarray_t;

struct {
	char *location;
	int count;
} tokenizer_t;

bool is_whitespace(char c);
bool is_eol(char c);
bool is_letter(char c);
void ignore_comments_and_whitespace(tokenizer_t& tokenizer);
token_t get_token(tokenizer_t& tokenizer);
void DeleteTokenContents(tokenarray_t token_array);
void DeleteTokens(tokenarray_t token_array);
void InitializeTokenArray(tokenarray_t& token_array, unsigned int size);
void ResizeTokenArray(tokenarray_t& token_array, unsigned int size);
tokenarray_t LexInput(char *input);
void DebugPrintTokenArray(tokenarray_t token_array);

#endif // __LEXER_H_