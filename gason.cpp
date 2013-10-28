#include <stdlib.h>
#include <math.h>
#include "gason.h"

static unsigned char ctype[256];

static const struct ctype_init_t
{
	ctype_init_t()
	{
		for (int i : "\t\n\v\f\r\x20") ctype[i] |= 001;
		for (int i : ",]}") ctype[i] |= 002;
		for (int i : "+-") ctype[i] |= 004;
		for (int i : "0123456789") ctype[i] |= 010;
		for (int i : "ABCDEF" "abcdef") ctype[i] |= 020;
	}
} ctype_init;

inline bool is_space(char c) { return (ctype[(int)(unsigned char)c] & 001) != 0; }
inline bool is_delim(char c) { return (ctype[(int)(unsigned char)c] & 003) != 0; }
inline bool is_sign(char c) { return (ctype[(int)(unsigned char)c] & 004) != 0; }
inline bool is_dec(char c) { return (ctype[(int)(unsigned char)c] & 010) != 0; }
inline bool is_hex(char c) { return (ctype[(int)(unsigned char)c] & 030) != 0; }

inline int char2int(char c)
{
	if (c >= 'a') return c - 'a' + 10;
	if (c >= 'A') return c - 'A' + 10;
	return c - '0';
}

static double str2float(const char *str, char **endptr)
{
	double sign = 1;
	if (is_sign(*str)) sign = ',' - *str++;
	double result = 0;
	while (is_dec(*str)) result = (result * 10) + (*str++ - '0');
	if (*str == '.')
	{
		++str;
		double base = 1;
		while (is_dec(*str)) base *= 0.1, result += (*str++ - '0') * base;
	}
	double exponent = 0;
	if (*str == 'e' || *str == 'E')
	{
		++str;
		double sign = 1;
		if (is_sign(*str)) sign = ',' - *str++;
		while (is_dec(*str)) exponent = (exponent * 10) + (*str++ - '0');
		exponent *= sign;
	}
	*endptr = (char *)str;
	return sign * result * pow(10, exponent);
}

JsonAllocator::~JsonAllocator()
{
	while (head)
	{
		Zone *temp = head->next;
		free(head);
		head = temp;
	}
}

inline void *align_pointer(void *x, size_t align) { return (void *)(((uintptr_t)x + (align - 1)) & ~(align - 1)); }

void *JsonAllocator::allocate(size_t n, size_t align)
{
	if (head)
	{
		char *p = (char *)align_pointer(head->end, align);
		if (p + n <= (char *)head + 4096)
		{
			head->end = p + n;
			return p;
		}
	}
	size_t zone_size = sizeof(Zone) + n + align;
	Zone *z = (Zone *)malloc(zone_size > 4096 ? zone_size : 4096);
	char *p = (char *)align_pointer(z + 1, align);
	if (head && zone_size > 4096)
	{
		z->next = head->next;
		head->next = z;
	}
	else
	{
		z->next = head;
		head = z;
	}
	z->end = p + n;
	return p;
}

struct JsonList
{
	JsonTag tag;
	JsonValue key;
	JsonValue head;
	JsonValue tail;

	void reset(JsonTag t)
	{
		tag = t;
		key = tail = JsonValue();
		head = JsonValue(tag, nullptr);
	}

	template<typename T> void append(T *p)
	{
		if (tail)
		{
			((T *)tail.getPayload())->next = p;
			tail = JsonValue(tag, p);
		}
		else
		{
			head = tail = JsonValue(tag, p);
		}
	}
};

JsonParseStatus json_parse(char *str, char **endptr, JsonValue *value, JsonAllocator &allocator)
{
	const int stack_size = 128;
	JsonList stack[stack_size];
	int top = -1;

	while (*str)
	{
		JsonValue o;
		while (*str && is_space(*str))
		{
			++str;
		}
		*endptr = str++;
		switch (**endptr)
		{
			case '\0':
				continue;
			case '-':
				if (!is_dec(*str) && *str != '.') return JSON_PARSE_BAD_NUMBER;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				o = JsonValue(str2float(*endptr, &str));
				if (!is_delim(*str)) return *endptr = str, JSON_PARSE_BAD_NUMBER;
				break;
			case '"':
				o = JsonValue(JSON_TAG_STRING, str);
				for (char *s = str; *str; ++s, ++str)
				{
					int c = *s = *str;
					if (c == '\\')
					{
						c = *++str;
						switch (c)
						{
							case '\\':
							case '"':
							case '/': *s = c; break;
							case 'b': *s = '\b'; break;
							case 'f': *s = '\f'; break;
							case 'n': *s = '\n'; break;
							case 'r': *s = '\r'; break;
							case 't': *s = '\t'; break;
							case 'u':
								c = 0;
								for (int i = 0; i < 4; ++i)
								{
									if (!is_hex(*++str)) return *endptr = str, JSON_PARSE_BAD_STRING;
									c = c * 16 + char2int(*str);
								}
								if (c <= 0x7F)
								{
									*s = c;
								}
								else if (c <= 0x7FF)
								{
									*s++ = 0xC0 | (c >> 6);
									*s = 0x80 | (c & 0x3F);
								}
								else if (c <= 0xFFFF)
								{
									*s++ = 0xE0 | (c >> 12);
									*s++ = 0x80 | ((c >> 6) & 0x3F);
									*s = 0x80 | (c & 0x3F);
								}
								break;
							default:
								return *endptr = str, JSON_PARSE_BAD_STRING;
						}
					}
					else if (c == '"')
					{
						*s = 0;
						++str;
						break;
					}
				}
				if (!is_delim(*str) && *str != ':') return *endptr = str, JSON_PARSE_BAD_STRING;
				break;
			case 't':
				for (const char *s = "rue"; *s; ++s, ++str)
				{
					if (*s != *str) return *endptr = str, JSON_PARSE_UNKNOWN_IDENTIFIER;
				}
				o = JsonValue(JSON_TAG_BOOL, (void *)true);
				break;
			case 'f':
				for (const char *s = "alse"; *s; ++s, ++str)
				{
					if (*s != *str) return *endptr = str, JSON_PARSE_UNKNOWN_IDENTIFIER;
				}
				o = JsonValue(JSON_TAG_BOOL, (void *)false);
				break;
			case 'n':
				for (const char *s = "ull"; *s; ++s, ++str)
				{
					if (*s != *str) return *endptr = str, JSON_PARSE_UNKNOWN_IDENTIFIER;
				}
				break;
			case '[':
				if (++top == stack_size) return JSON_PARSE_OVERFLOW;
				stack[top].reset(JSON_TAG_ARRAY);
				continue;
			case ']':
				if (top == -1) return JSON_PARSE_UNDERFLOW;
				if (stack[top].tag != JSON_TAG_ARRAY) return JSON_PARSE_MISMATCH_BRACKET;
				o = stack[top--].head;
				break;
			case '{':
				if (++top == stack_size) return JSON_PARSE_OVERFLOW;
				stack[top].reset(JSON_TAG_OBJECT);
				continue;
			case '}':
				if (top == -1) return JSON_PARSE_UNDERFLOW;
				if (stack[top].tag != JSON_TAG_OBJECT) return JSON_PARSE_MISMATCH_BRACKET;
				o = stack[top--].head;
				break;
			case ':':
			case ',':
				continue;
			default:
				return JSON_PARSE_UNEXPECTED_CHARACTER;
		}

		if (top == -1)
		{
			stack[0].head = o;
			break;
		}

		switch (stack[top].tag)
		{
			case JSON_TAG_ARRAY:
				{
					JsonElement *p = (JsonElement *)allocator.allocate(sizeof(JsonElement));
					p->next = nullptr;
					p->value = o;
					stack[top].append(p);
				}
				break;
			case JSON_TAG_OBJECT:
				if (stack[top].key)
				{
					JsonPair *p = (JsonPair *)allocator.allocate(sizeof(JsonPair));
					p->next = nullptr;
					p->value = o;
					p->key = stack[top].key.toString();
					stack[top].append(p);
					stack[top].key = JsonValue();
				}
				else
				{
					stack[top].key = o;
				}
				break;
			default:
				continue;
		}
	}

	*value = stack[0].head;

	return JSON_PARSE_OK;
}