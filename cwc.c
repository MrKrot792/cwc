// Little docs:
// `cwc site build-site` -> compile `site` into `build-site`
// `cwc` -> same as `cwc site build-site`
// `cwc some_site` -> also valid, `some_site` is compiled into `build-site`

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>

#define TEMPLATE_FILE "template.html"
// these are just rarely used together in html
#define TEMPLATE_SYMBOL "@%"
#define TEMPLATE_SYMBOL_LENGTH (sizeof(TEMPLATE_SYMBOL)-1)

#define DEFAULT_INPUT_DIR "site/"
#define DEFAULT_OUTPUT_DIR "build-site/"

#define RELATIVE_PATH_PREFIX "./"

#define IGNORE_DIRECTORY "assets/"

char* err_str;

// This mallocs. Make sure to `free` this string
#define PATH_CONC(output, base, file)			\
  do {							\
    *output = malloc(strlen(base) + strlen(file) + 1);	\
    strcpy(*output, base);				\
    strcat(*output, file);				\
  } while (0)						\

#define HANDLE_ERROR_VALUE(e, c)					\
  do {									\
    if (e == c) {							\
      asprintf(&err_str, "%s:%d", __PRETTY_FUNCTION__, __LINE__);	\
      perror(err_str);							\
      exit(errno);							\
    }									\
  } while (0)								\
    
#define HANDLE_ERROR(e) HANDLE_ERROR_VALUE(e, -1)

#define UNBALANCED_TEMPLATE_SYMBOL_ERROR(file_str)			\
  do {									\
    fprintf(stderr, "Error: unbalanced template symbol at %s\n", file_str); \
    exit(-1);								\
  } while (0)								\

// strdup cuz we may modify it later
void setIoDirs(int argc, char** argv, char** input_dir, char** output_dir) {
  if (argc == 1) {
    *input_dir = strdup(DEFAULT_INPUT_DIR);
    *output_dir = strdup(DEFAULT_OUTPUT_DIR);
  } else if (argc == 2) {
    *input_dir = strdup(argv[1]);
    *output_dir = strdup(DEFAULT_OUTPUT_DIR);
  } else if (argc == 3) {
    *input_dir = strdup(argv[1]);
    *output_dir = strdup(argv[2]);
  } else {
    fprintf(stderr, "yo that's too much arguments, calm down bro\n");
    exit(1);
  }
}

typedef struct {
  char* data;
  size_t size;
} OwningStringView;

typedef struct {
  const char* data;
  size_t size;
} StringView;

typedef struct {
  size_t length;
  size_t capacity;
  StringView* tokens;
  char* original_string; // this is \0 terminated
} TemplateFile;

OwningStringView readFile(const char *filename) {
  FILE *fp = fopen(filename, "rb"); // rb just in case, it does nothing on linux
  if(!fp) return (OwningStringView){.data = NULL, .size = 0};

  fseek(fp, 0, SEEK_END);
  int fsize = ftell(fp);
  rewind(fp);

  char* fcontent = malloc(sizeof(char) * fsize);
  fread(fcontent, 1, fsize, fp);
  fclose(fp);

  return (OwningStringView){ .data = fcontent, .size = fsize};
}

char* readFileC(const char* filename) {
  FILE *fp = fopen(filename, "rb"); // rb just in case, it does nothing on linux
  if(!fp) return NULL;

  fseek(fp, 0, SEEK_END);
  int fsize = ftell(fp);
  rewind(fp);

  char* fcontent = malloc(sizeof(char) * fsize + 1);
  fread(fcontent, 1, fsize, fp);
  fclose(fp);
  fcontent[fsize-1] = '\0';

  return fcontent;
}

void freeOwingStringView(OwningStringView* str) {
  free(str->data);
}

// i'm sorry it's 2 AM i don't know what to call these
typedef struct {
  StringView token; // %body% for example
  StringView body;  // <p>something</p> for example
} RegularFileToken;

typedef struct {
  size_t length;
  size_t capacity;
  RegularFileToken* tokens;
  char* original_text;
} RegularFile;

RegularFile serializeFile(const char* file_path) {
  RegularFile result = {0};
  result.original_text = readFileC(file_path);
  result.capacity = 4;
  result.tokens = malloc(result.capacity * sizeof(RegularFileToken));

  char* text_to_search = result.original_text;
  
  while (1) {
    char* first = strstr(text_to_search, TEMPLATE_SYMBOL);
    if (first == NULL) break;
    if (first != result.original_text) {
      if (*(first-1) == '\\') {
	text_to_search = first + TEMPLATE_SYMBOL_LENGTH; // to skip the template symbol
	continue;
      }
    }
    char* second = strstr(first+TEMPLATE_SYMBOL_LENGTH, TEMPLATE_SYMBOL);
    if (second == NULL) UNBALANCED_TEMPLATE_SYMBOL_ERROR(file_path);

    size_t length = second - first;

    size_t till_next_template_symbol = 0;
    char* next_template_symbol = strstr(second + TEMPLATE_SYMBOL_LENGTH, TEMPLATE_SYMBOL);
    if (next_template_symbol == NULL)
      till_next_template_symbol = strlen(second + TEMPLATE_SYMBOL_LENGTH); // means we reached the end
    else
      till_next_template_symbol = next_template_symbol - (second + TEMPLATE_SYMBOL_LENGTH);

    if (result.capacity <= result.length+1) {
      result.capacity += 4;
      result.tokens = realloc(result.tokens, result.capacity * sizeof(RegularFileToken));
    }
    result.tokens[result.length] = (RegularFileToken) {
      .token = (StringView) {.data = first, .size = length},
      .body = (StringView) {.data = second+TEMPLATE_SYMBOL_LENGTH, .size = till_next_template_symbol}
    };
      
    result.length++;

    text_to_search = second+TEMPLATE_SYMBOL_LENGTH;
  }

  return result;
}

void freeRegularFile(RegularFile* rf) {
  free(rf->original_text);
  free(rf->tokens);
}

// the processed file is at `output_file_path`
// kinda n(O^2)
void processOneFile(TemplateFile template, const char* file_path, const char* output_file_path) {
  printf("[FILE] Processing %s file and outputing to %s.\n", file_path, output_file_path);
  RegularFile input_file = serializeFile(file_path);
  FILE* output_file = fopen(output_file_path, "w");

  const char* left_at_string = template.original_string;
  for (size_t i = 0; i < template.length; i++) {
    size_t till_next_token = template.tokens[i].data - left_at_string;
    // we write until we encounter a token
    fwrite(left_at_string, 1, till_next_token, output_file);

    size_t token_size = 0;
    // then we write the corresponding token from the file
    for (size_t j = 0; j < input_file.length; j++) {
      if (strncmp(template.tokens[i].data, input_file.tokens[j].token.data, template.tokens[i].size) == 0) {
	fwrite(input_file.tokens[j].body.data, 1, input_file.tokens[j].body.size, output_file);
	token_size = input_file.tokens[j].token.size;
      }
    }
    // means we couldn't find a token
    if (token_size == 0) {
      fprintf(stderr, "[  WARNING  ] No matching tag %.*s in the file %s.\n", (int)(template.tokens[i].size+TEMPLATE_SYMBOL_LENGTH), template.tokens[i].data, file_path);
    }

    left_at_string += till_next_token + token_size + TEMPLATE_SYMBOL_LENGTH;
  }
  fwrite(left_at_string, 1, strlen(left_at_string), output_file);
  freeRegularFile(&input_file);
}

void ensureDirExists(const char* path) {
  struct stat buf;
  if (stat(path, &buf) == -1) {
    mkdir(path, 0755); // idk why 0755 tho
  }
}

void copyFile(const char* path, const char* output_path) {
  OwningStringView input_file = readFile(path);
  FILE* output_file = fopen(output_path, "wb");

  fwrite(input_file.data, 1, input_file.size, output_file);
  
  freeOwingStringView(&input_file);
  fclose(output_file);
}

void processDirInternal(const char* path, const char* output_path, TemplateFile template, bool just_copy, const char* ignore_directory) {
  printf("[ DIR] Processing %s dir and outputing to %s.\n", path, output_path);

  ensureDirExists(output_path);

  just_copy = just_copy ? just_copy : strcmp(path, ignore_directory) == 0;
  
  DIR* input_dir = opendir(path);
  HANDLE_ERROR_VALUE(input_dir, NULL);

  struct dirent* current_file = NULL;
  struct stat stats = {0};

  // linux adds `.` and `..` for no reason
  current_file = readdir(input_dir);
  current_file = readdir(input_dir);
  
  while ((current_file = readdir(input_dir))) {
    char* current_file_path = NULL;
    PATH_CONC(&current_file_path, path, current_file->d_name);
    
    stat(current_file_path, &stats);
    switch(stats.st_mode & S_IFMT) {
    case S_IFREG: { // if it's a file
      if (strcmp(current_file->d_name, TEMPLATE_FILE) == 0) goto loopEnd;
      char* output_file_path = NULL;
      PATH_CONC(&output_file_path, output_path, current_file->d_name);

      if (just_copy) copyFile(current_file_path, output_file_path);
      else processOneFile(template, current_file_path, output_file_path);
      
      free(output_file_path);
      break;
    }
    case S_IFDIR: { // if it's a dir
      // we recurse
      char* output_dir_path = NULL;
      PATH_CONC(&output_dir_path, output_path, current_file->d_name);
      size_t len = strlen(output_dir_path);
      output_dir_path = realloc(output_dir_path, len + 2);
      output_dir_path[len] = '/';
      output_dir_path[len+1] = '\0';

      size_t len2 = strlen(current_file_path);
      current_file_path = realloc(current_file_path, len2 + 2);
      current_file_path[len2] = '/';
      current_file_path[len2+1] = '\0';
      
      processDirInternal(current_file_path, output_dir_path, template, just_copy, ignore_directory);
      free(output_dir_path);
      break;
    }
    case S_IFLNK:
      fprintf(stderr, "Yeah, no symlinks for now, sorry.\n");
      assert(false);
      break;
    }
  loopEnd:
    free(current_file_path);
  }
  
  closedir(input_dir);
}

void processDir(const char* path, const char* output_path, TemplateFile template, const char* ignore_directory) {
  processDirInternal(path, output_path, template, false, ignore_directory);
}

char* makeActualPath(char* input) {
  size_t len = strlen(input);
  bool ends_in_slash = false;
  if (input[len-1] == '/') ends_in_slash = true;

  size_t relative_path_prefix_len = strlen(RELATIVE_PATH_PREFIX);
  size_t add_len = relative_path_prefix_len + !ends_in_slash + 1;
  char* result = (char*) malloc((len + add_len) * sizeof(char));

  memcpy(result, RELATIVE_PATH_PREFIX, relative_path_prefix_len);
  memcpy(result+relative_path_prefix_len, input, len);
  if (!ends_in_slash) result[(len + add_len) - 2] = '/'; // right before \0
  result[(len + add_len) - 1] = '\0';
  return result;
}

// Result is okay to use only before caller's function return
TemplateFile processTemplate(const char* template_path) {
  TemplateFile result = {0};

  result.original_string = readFileC(template_path);
  
  result.capacity = 4;
  result.tokens = malloc(result.capacity * sizeof(StringView));
  
  const char* string = result.original_string;
  while (1) {
    char* first = strstr(string, TEMPLATE_SYMBOL);
    if (first == NULL) break;
    if (first != result.original_string) {
      if (*(first-1) == '\\') {
	string = first + TEMPLATE_SYMBOL_LENGTH;
	continue;
      }
    }
    char* second = strstr(first+TEMPLATE_SYMBOL_LENGTH, TEMPLATE_SYMBOL);
    if (second == NULL) UNBALANCED_TEMPLATE_SYMBOL_ERROR(template_path);

    size_t length = second - first;
    if (result.capacity <= result.length+1) {
      result.capacity += 4;
      result.tokens = realloc(result.tokens, result.capacity);
    }
    result.tokens[result.length] = (StringView){.data = first, .size = length};
    result.length++;
    string = second + TEMPLATE_SYMBOL_LENGTH;
  }

  return result;
}

void freeTemplate(TemplateFile* tf) {
  free(tf->original_string);
  free(tf->tokens);
}

int main(int argc, char** argv) {
  char* base_input_dir_str;
  char* base_output_dir_str;

  setIoDirs(argc, argv, &base_input_dir_str, &base_output_dir_str);

  char* input_dir_str = makeActualPath(base_input_dir_str);
  char* output_dir_str = makeActualPath(base_output_dir_str);

  char* template_file_path = NULL;
  PATH_CONC(&template_file_path, input_dir_str, TEMPLATE_FILE);
  TemplateFile template = processTemplate(template_file_path);
  free(template_file_path);

  char* ignore_directory = NULL;
  PATH_CONC(&ignore_directory, input_dir_str, IGNORE_DIRECTORY);
  
  processDir(input_dir_str, output_dir_str, template, ignore_directory);

  freeTemplate(&template);
  free(err_str);
  free(input_dir_str);
  free(output_dir_str);
  free(base_input_dir_str);
  free(base_output_dir_str);
  free(ignore_directory);
}
