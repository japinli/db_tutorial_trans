#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#define unused(expr) ((void) (expr))

struct InputBuffer_t
{
    char      *buffer;
    size_t     buffer_length;
    ssize_t    input_length;
};
typedef struct InputBuffer_t InputBuffer;

enum ExecuteResult_t
{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_UNKNOWN_STMT
};
typedef enum ExecuteResult_t ExecuteResult;

enum MetaCommandResult_t
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
};
typedef enum MetaCommandResult_t MetaCommandResult;

enum PrepareResult_t
{
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
};
typedef enum PrepareResult_t PrepareResult;

enum StatementType_t
{
    STATEMENT_INSERT,
    STATEMENT_SELECT
};
typedef enum StatementType_t StatementType;

#define COLUMN_USERNAME_SIZE    32
#define COLUMN_EMAIL_SIZE       255
struct Row_t
{
    uint32_t    id;
    char        username[COLUMN_USERNAME_SIZE + 1];
    char        email[COLUMN_EMAIL_SIZE + 1];
};
typedef struct Row_t Row;

struct Statement_t
{
    StatementType type;
    Row           row_to_insert;  /* Only used by insert statement */
};
typedef struct Statement_t Statement;

#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

#define ID_SIZE          size_of_attribute(Row, id)
#define USERNAME_SIZE    size_of_attribute(Row, username)
#define EMAIL_SIZE       size_of_attribute(Row, email)
#define ID_OFFSET        0
#define USERNAME_OFFSET  (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET     (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE         (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)

#define PAGE_SIZE        4096
#define TABLE_MAX_PAGES  100

typedef struct Pager_t
{
    int       file_descriptor;
    uint32_t  file_length;
    uint32_t  num_pages;
    void     *pages[TABLE_MAX_PAGES];
} Pager;

struct Table_t
{
    Pager      *pager;
    uint32_t    root_page_num;
};
typedef struct Table_t Table;

typedef struct {
    Table    *table;
    uint32_t  page_num;
    uint32_t  cell_num;
    bool      end_of_table; /* Indicates a position one past the last element */
} Cursor;

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

/*
 * Common Node Header Layout
 */
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint8_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

/*
 * Leaf Node Header Layout
 */
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE =
    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

/*
 * Leaf Node Body Layout
 */
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

void print_prompt();
void print_row(Row *row);
void print_constants();
void print_leaf_node(void* node);

InputBuffer *new_input_buffer();
void read_input(InputBuffer *input_buffer);

MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table);
PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);
ExecuteResult execute_statement(Statement *statement, Table *table);

Table *db_open(const char *filename);
void db_close(Table *table);
void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);

Pager *pager_open(const char *filename);
void *get_page(Pager *pager, uint32_t page_num);
void pager_flush(Pager* pager, uint32_t page_num);

Cursor *table_start(Table *table);
Cursor *table_end(Table *table);

void cursor_advance(Cursor *cursor);
void *cursor_value(Cursor *cursor);

void initialize_leaf_node(void *node);
uint32_t *leaf_node_num_cells(void *node);
void *leaf_node_cell(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
void *leaf_node_value(void *node, uint32_t cell_num);
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);


void
print_prompt()
{
    printf("db > ");
}

void
print_row(Row *row)
{
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void
print_constants()
{
    printf("ROW_SIZE: %lu\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void
print_leaf_node(void* node)
{
    uint32_t i;
    uint32_t num_cells = *leaf_node_num_cells(node);

    printf("leaf (size %d)\n", num_cells);
    for (i = 0; i < num_cells; i++) {
        uint32_t key = *leaf_node_key(node, i);
        printf("  - %d : %d\n", i, key);
    }
}

InputBuffer *
new_input_buffer()
{
    InputBuffer *input_buffer = (InputBuffer *) malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void
read_input(InputBuffer *input_buffer)
{
    ssize_t bytes_read;

    bytes_read = getline(&input_buffer->buffer,
                         &input_buffer->buffer_length,
                         stdin);

    if (bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    /* Ignore trailing newline */
    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
}

MetaCommandResult
do_meta_command(InputBuffer *input_buffer, Table *table)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        db_close(table);
        exit(EXIT_SUCCESS);
    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
        printf("Tree:\n");
        print_leaf_node(get_page(table->pager, 0));
        return META_COMMAND_SUCCESS;
    } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
        printf("Constants:\n");
        print_constants();
        return META_COMMAND_SUCCESS;
    }

    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

PrepareResult
prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        return prepare_insert(input_buffer, statement);
    }
    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

PrepareResult
prepare_insert(InputBuffer *input_buffer, Statement *statement)
{
    char *keyword = strtok(input_buffer->buffer, " ");
    char *id_string = strtok(NULL, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");

    unused(keyword);

    statement->type = STATEMENT_INSERT;

    if (id_string == NULL || username == NULL || email == NULL) {
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string);
    if (id < 0) {
        return PREPARE_NEGATIVE_ID;
    }
    if (strlen(username) > COLUMN_USERNAME_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

ExecuteResult
execute_insert(Statement *statement, Table *table)
{
    Row    *row_to_insert;
    Cursor *cursor;
    void   *node = get_page(table->pager, table->root_page_num);

    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
        return EXECUTE_TABLE_FULL;
    }

    row_to_insert = &statement->row_to_insert;
    cursor = table_end(table);

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

    free(cursor);

    return EXECUTE_SUCCESS;
}

ExecuteResult
execute_select(Statement *statement, Table *table)
{
    Cursor *cursor = table_start(table);
    Row     row;

    unused(statement);

    while (!(cursor->end_of_table)) {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }

    free(cursor);

    return EXECUTE_SUCCESS;
}

ExecuteResult
execute_statement(Statement *statement, Table *table)
{
    switch (statement->type) {
    case STATEMENT_INSERT:
        return execute_insert(statement, table);
    case STATEMENT_SELECT:
        return execute_select(statement, table);
    }

    return EXECUTE_UNKNOWN_STMT;
}

Table *
db_open(const char *filename)
{
    Pager    *pager = pager_open(filename);
    Table    *table = (Table *)malloc(sizeof(Table));

    table->pager = pager;
    table->root_page_num = 0;

    if (pager->num_pages == 0) {
        /* New database file. Initialize page 0 as leaf node. */
        void *root_node = get_page(pager, 0);
        initialize_leaf_node(root_node);
    }

    return table;
}

void
db_close(Table *table)
{
    Pager     *pager = table->pager;
    uint32_t   i;
    int        result;

    for (i = 0; i < pager->num_pages; i++) {
        if (pager->pages[i] == NULL) {
            continue;
        }

        pager_flush(pager, i);
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    result = close(pager->file_descriptor);
    if (result == -1) {
        printf("Error closing db file.\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < TABLE_MAX_PAGES; i++) {
        void *page = pager->pages[i];
        if (page) {
            free(page);
            pager->pages[i] = NULL;
        }
    }
    free(pager);
    free(table);
}

void
serialize_row(Row *source, void *destination)
{
    char *dest = (char *) destination;
    memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(dest + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(dest + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void
deserialize_row(void *source, Row *destination)
{
    char *src = (char *) source;
    memcpy(&(destination->id), src + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), src + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), src + EMAIL_OFFSET, EMAIL_SIZE);
}

Pager *
pager_open(const char *filename)
{
    int        fd;
    off_t      file_length;
    uint32_t   i;
    Pager     *pager;

    fd = open(filename,
              O_RDWR |      /* Read/Write mode */
              O_CREAT,      /* Create file if it does not exist */
              S_IWUSR |     /* User write permission */
              S_IRUSR);     /* User Read permission */

    if (fd == -1) {
        printf("Unable to open file\n");
        exit(EXIT_FAILURE);
    }

    file_length = lseek(fd, 0, SEEK_END);

    pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE != 0) {
        printf("Db file is not a whole number of pages. Corrupt file.\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }

    return pager;
}

void *
get_page(Pager *pager, uint32_t page_num)
{
    if (page_num > TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds. %d > %d\n",
               page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] == NULL) {
        /* Cache miss. Allocate memory and load from file. */
        void      *page = malloc(PAGE_SIZE);
        uint32_t   num_pages = pager->file_length / PAGE_SIZE;

        /* We might save a partial page at the end of the file. */
        if (pager->file_length % PAGE_SIZE) {
            num_pages += 1;
        }

        if (page_num < num_pages) {
            ssize_t bytes_read;
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[page_num] = page;

        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }

    return pager->pages[page_num];
}

void
pager_flush(Pager* pager, uint32_t page_num)
{
    off_t	offset;
    ssize_t  bytes_written;
    if (pager->pages[page_num] == NULL) {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }

    offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

    if (offset == -1) {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    bytes_written =
        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

    if (bytes_written == -1) {
        printf("Error writing: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

Cursor *
table_start(Table *table)
{
    uint32_t  num_cells;
    void     *root_node;
    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));

    cursor->table = table;
    cursor->page_num = table->root_page_num;
    cursor->cell_num = 0;

    root_node = get_page(table->pager, table->root_page_num);
    num_cells = *leaf_node_num_cells(root_node);
    cursor->end_of_table = (num_cells == 0);

    return cursor;
}

Cursor *
table_end(Table *table)
{
    uint32_t  num_cells;
    void     *root_node;
    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));

    cursor->table = table;
    cursor->page_num = table->root_page_num;

    root_node = get_page(table->pager, table->root_page_num);
    num_cells = *leaf_node_num_cells(root_node);
    cursor->cell_num = num_cells;
    cursor->end_of_table = true;

    return cursor;
}

void
cursor_advance(Cursor *cursor)
{
    uint32_t  page_num = cursor->page_num;
    void     *node = get_page(cursor->table->pager, page_num);

    cursor->cell_num += 1;
    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
        cursor->end_of_table = true;
    }
}

void *
cursor_value(Cursor *cursor)
{
    uint32_t   page_num = cursor->page_num;
    void      *page = get_page(cursor->table->pager, page_num);

    return leaf_node_value(page, cursor->cell_num);
}

void
initialize_leaf_node(void *node)
{
    *leaf_node_num_cells(node) = 0;
}

uint32_t *
leaf_node_num_cells(void *node)
{
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

void *
leaf_node_cell(void *node, uint32_t cell_num)
{
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t *
leaf_node_key(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num);
}

void *
leaf_node_value(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void
leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cells = *leaf_node_num_cells(node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        /* Node full */
        printf("Need to implement splitting a leaf node.\n");
        exit(EXIT_FAILURE);
    }

    if (cursor->cell_num < num_cells) {
        /* Make room for new cell */
        uint32_t i;
        for (i = num_cells; i > cursor->cell_num; i--) {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
                   LEAF_NODE_CELL_SIZE);
        }
    }

    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

int
main(int argc, char *argv[])
{
    char           *filename;
    Table          *table;
    InputBuffer    *input_buffer;

    if (argc < 2) {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    filename = argv[1];
    table = db_open(filename);
    input_buffer = new_input_buffer();

    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
            case META_COMMAND_SUCCESS:
                continue;
            case META_COMMAND_UNRECOGNIZED_COMMAND:
                printf("Unrecognized command '%s'\n", input_buffer->buffer);
                continue;
            }
        }

        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
        case PREPARE_SUCCESS:
            break;
        case PREPARE_NEGATIVE_ID:
            printf("ID must be positive.\n");
            continue;
        case PREPARE_STRING_TOO_LONG:
            printf("String is too long.\n");
            continue;
        case PREPARE_SYNTAX_ERROR:
            printf("Syntax error. Could not parse statement.\n");
            continue;
        case PREPARE_UNRECOGNIZED_STATEMENT:
            printf("Unrecognized keyword at start of '%s'.\n",
                   input_buffer->buffer);
            continue;
        }

        switch (execute_statement(&statement, table)) {
        case EXECUTE_SUCCESS:
            printf("Executed.\n");
            break;
        case EXECUTE_TABLE_FULL:
            printf("Error: Table full.\n");
            break;
        case EXECUTE_UNKNOWN_STMT:
            printf("Error: Unknown statement.\n");
            break;
        }
    }

    return 0;
}
