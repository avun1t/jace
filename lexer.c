#include <stdio.h>
#include "lexer.h"

bool is_whitespace(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\f') || (c == '\v');
}

bool is_eol(char c)
{
	return (c == '\n') || (c == '\r');
}

bool is_letter(char c)
{
	bool result = false;

	if ((c >= 'A') && (c <= 'Z')) result = true;
	if ((c >= 'a') && (c <= 'z')) result = true;
	return result;

	// return (((c >= 'A') && (c <= 'Z')) ||
	// 		   ((c >= 'a') && (c <= 'z'))) ? true : false;
}

bool is_numeric(char c)
{
	bool result = false;

	if ((c >= '0') && (c <= '9')) result = true
	return result;

	// return ((c >= '0') && (c <= '9')) ? true : false;
}

token_t get_token(tokenizer_t &tokenizer)
{
	token_t token = {};
	token.length = 0;

	ignore_comments_and_whitespace(tokenizer);

	switch (tokenizer.location[0]) {
		case '\0':token.type = TokenType_EOF; break;
		case '(': token.type = TokenType_LPAREN; break;
		case ')': token.type = TokenType_RPAREN; break;
		case '{': token.type = TokenType_LBRACE; break;
		case '}': token.type = TokenType_RBRACE; break;
		case '[': token.type = TokenType_LBRACK; break;
		case ']': token.type = TokenType_RBRACK; break;

		case '+': token.type = TokenType_PLUS; break;
		case '-': token.type = TokenType_MINUT; break;
		case '*': token.type = TokenType_TIMES; break;
		case '/': token.type = TokenType_SLASH; break;

		case ';': token.type = TokenType_SEMICOLON; break;
		case ':': token.type = TokenType_COMMA; break;
		case '.': token.type = TokenType_PERIOD; break;

		case '=': token.type = TokenType_EQL; break;
		case '>': token.type = TokenType_GTR; break;
		case '<': token.type = TokenType_LSS; break;
		case '!': token.type = TokenType_NOT; break;

		// TODO: single quotes are skipped for now
		// NOTE: should everything in the quotes be a single token,
		// or each quote symbol is a token,
		// and every token in between can be interpreted as a quote
		case '"':
		{
			// we need to skip the " and start copying from the next char
			tokenizer.location++;

			token.type = TokenType_STRING;
			char *start_loc = tokenizer.location;

			while (tokenizer.location[0] != '"') {
				tokenizer.location++;
				token.length++;

				// NOTE: how do we properly handle unclosed quotation?
				// if we reached the end of the input we know for sure something went wrong
				if (tokenizer.location[0] == '\0') {
					// skip copying for now
					token.type = TokenType_UNKNOWN;
					break;
				}
			}

			token.contents = new char[token.length + 1];
			int iterator = 0;
			while (start_loc != tokenizer.location) {
				token.contents[iterator] = *start_loc;
				start_loc++;
				iterator++;
			}
			token.contents[token.length] = '\0';

			// also skip the end "
			tokenizer.location++;
		} break;

		default:
		{
			if (is_letter(tokenizer.location[0])) {
				// we put a pointer at the beginning, then iterate to the end
				// of the identifier, and then copy everything in between into
				// out token - much more efficient than copying 1 letter at a time
				char *start_loc = tokenizer.location;
				token.type = TokenType_IDENTIFIER;

				while (is_letter(tokenizer.location[0]) || is_numeric(tokenizer.location[0]) || tokenizer.location[0] == '_') {
					tokenizer.location++;
					token.length++;
					// TODO: should we limit?
					if (token.length == 255) break;
				}

				token.contents = new char[token.length + 1];
				int iterator = 0;
				while (start_loc != tokenizer.location) {
					token.contents[iterator] = *start_loc;
					start_loc++;
					iterator++;
				}
				token.contents[token.length] = '\0';

				// once we have an identifier, we can check if it matches a keyword
				// NOTE: put this inside jace.c for the actual highlighting?
				//if (strcompare(token.contents, "for")) {
				//	token.type = TokenType_FOR;
				//	break;
				//}
				//if (strcompare(token.contents, "while")) {
				//	token.type = TokenType_WHILE;
				//	break;
				//}
				//if (strcompare(token.contents, "return")) {
				//	token.type = TokenType_RETURN;
				//	break;
				//}
				//if (strcompare(token.contents, "do")) {
				//	token.type = TokenType_DO;
				//	break;
				//}
				//if (strcompare(token.contents, "new")) {
				//	token.type = TokenType_NEW;
				//	break;
				//}
				//if (strcompare(token.contents, "delete")) {
				//	token.type = TokenType_DELETE;
				//	break;
				//}
				//if (strcompare(token.contents, "null")) {
				//	token.type = TokenType_NULL;
				//	break;
				//}

				// TODO: before we avoided copies, now we copy early
				// we save the copy to the end to avoid copying for keywords
				//strcpy(token.contents, buffer);

				// for boolean values we can change our token type
				//if (strcmp(token.contents, "true")) || strcmp(token.contents, "false") {
				//	token.type = TokenType_BOOL;
				//}
			} else if (is_numeric(tokenizer.location[0])) {
				// TODO: should we eat all leading 0's in a numerical value
				// other than the first 0 for a float?
				char *start_loc = tokenizer.location;
				token.type = TokenType_DIGIT;

				while (is_numeric(tokenizer.location[0])) {
					tokenizer.location++;
					token.length++;
					// TODO: should we limit ourselves?

					// look forward 1 character for the . operator for floats
					if (tokenizer.location[0] == '.') {
						token.type = TokenType_FLOAT;
						tokenizer.location++;
						token.length++;
					}

					// TODO: we should probably check for multiple '.', because that's not a real float
				}
				token.contents = new char[token.length + 1];
				int iterator = 0;
				while (start_loc != tokenizer.location) {
					token.contents[iterator] = *start_loc;
					start_loc++;
					iterator++;
				}
				token.contents[token.length] = '\0';
			}
		} break;
	}
	return token;
}

void delete_token_contents(tokenarray_t token_array)
{
	for (int i = 0; i < token_array.count; i++) {
		token_array.tokens[i].contents = '\0';
	}
}

void delete_tokens(tokenarray_t token_array)
{
	delete_token_contents(token_array);
	token_array.tokens = 0;
	token_array = {};
}