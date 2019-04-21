#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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
    char        username[COLUMN_USERNAME_SIZE];
    char        email[COLUMN_EMAIL_SIZE];
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
#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)

struct Table_t
{
    void       *pages[TABLE_MAX_PAGES];
    uint32_t    num_rows;
};
typedef struct Table_t Table;


void print_row(Row *row);
InputBuffer *new_input_buffer();
void print_prompt();
void read_input(InputBuffer *input_buffer);
MetaCommandResult do_meta_command(InputBuffer *input_buffer);
PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);
ExecuteResult execute_statement(Statement *statement, Table *table);
Table *new_table();
void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);
void *row_slot(Table *table, uint32_t row_num);


void
print_row(Row *row)
{
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
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
print_prompt()
{
    printf("db > ");
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
do_meta_command(InputBuffer *input_buffer)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        exit(EXIT_SUCCESS);
    }

    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

PrepareResult
prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;
        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s",
                                   &(statement->row_to_insert.id),
                                   statement->row_to_insert.username,
                                   statement->row_to_insert.email);
        if (args_assigned < 3) {
            return PREPARE_SYNTAX_ERROR;
        }
        return PREPARE_SUCCESS;
    }
    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult
execute_insert(Statement *statement, Table *table)
{
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }

    Row *row_to_insert = &statement->row_to_insert;
    serialize_row(row_to_insert, row_slot(table, table->num_rows));
    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

ExecuteResult
execute_select(Statement *statement, Table *table)
{
    Row row;
    for (uint32_t i = 0; i < table->num_rows; i++) {
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }

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
new_table()
{
    Table *table = (Table *)malloc(sizeof(Table));
    table->num_rows = 0;

    return table;
}

int
main(int argc, char *argv[])
{
    Table *table = new_table();
    InputBuffer *input_buffer = new_input_buffer();

    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer)) {
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
        }
    }

    return 0;
}

void
serialize_row(Row *source, void *destination)
{
    char *dest = (char *) destination;
    memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void
deserialize_row(void *source, Row *destination)
{
    char *src = (char *) source;
    memcpy(&(destination->id), src + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), src + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), src + EMAIL_OFFSET, EMAIL_SIZE);
}

void *
row_slot(Table *table, uint32_t row_num)
{
    uint32_t   page_num = row_num / ROWS_PER_PAGE;
    void      *page = table->pages[page_num];

    if (!page) {
        /* Allocate memory only when we try to access page */
        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }

    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
    uint32_t    byte_offset = row_offset * ROW_SIZE;
    return (char *) page + byte_offset;
}
