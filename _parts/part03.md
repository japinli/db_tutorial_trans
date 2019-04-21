---
title: 03 - 仅支持追加的单表内存数据库
date: 2019-04-20
---

本章我们将为我们的数据库一点点的添加更多的限制。目前，我们主要添加下面三个限制：

- 支持两个操作：插入一行记录和输出所有记录
- 数据仅驻留在内存（不会持久化到磁盘）
- 支持单个、硬编码的表

我们在本章实现的硬编码表将用来存储用户信息，它的格式如下：

| 属性列     | 类型          |
|------------|---------------|
| id         | integer       |
| username   | varchar(32)   |
| email      | varchar(255)  |

这是一个简单的模式 (schema)，但是它支持多种数据类型以及文本 (text) 数据类型的不同大小。

我们将要实现 `insert` 语句格式如下：

```
insert 1 cstack foo@bar.com
```

这意味着我们将要修改 `prepare_statement` 函数来解析参数。

```diff
     if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
         statement->type = STATEMENT_INSERT;
+        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s",
+                                   &(statement->row_to_insert.id),
+                                   statement->row_to_insert.username,
+                                   statement->row_to_insert.email);
+        if (args_assigned < 3) {
+            return PREPARE_SYNTAX_ERROR;
+        }
         return PREPARE_SUCCESS;
     }
     if (strncmp(input_buffer->buffer, "select", 6) == 0) {
```

我们将解析到的参数存放到新建的 `statement` 对象的 `Row` 数据结构中。

```diff
+#define COLUMN_USERNAME_SIZE    32
+#define COLUMN_EMAIL_SIZE       255
+struct Row_t
+{
+    uint32_t    id;
+    char        username[COLUMN_USERNAME_SIZE];
+    char        email[COLUMN_EMAIL_SIZE];
+};
+typedef struct Row_t Row;
+
 struct Statement_t
 {
     StatementType type;
+    Row           row_to_insert;  /* Only used by insert statement */
 };
 typedef struct Statement_t Statement;
```

现在我们需要将这些数据拷贝到表示表的数据结构中去。SQLite 使用 B-树来进行快速的查找、插入和删除数据。我们则从更为简单的起点开始。类似于 B-树，它将行记录分组到页面中，但是页面以数组而非 B-树的形式组织。

我们将按下面的方式进行：

* 行记录存储在内存中被称为页面的内存块中
* 每个页面尽可能多的存储行记录
* 页面中的行记录按顺序紧凑的存储在一起
* 页面按需进行分配
* 使用固定大小的指针数组来维护页面

首先，我们给出行记录的紧凑格式的定义：

```diff
+#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)
+
+#define ID_SIZE          size_of_attribute(Row, id)
+#define USERNAME_SIZE    size_of_attribute(Row, username)
+#define EMAIL_SIZE       size_of_attribute(Row, email)
+#define ID_OFFSET        0
+#define USERNAME_OFFSET  (ID_OFFSET + ID_SIZE)
+#define EMAIL_OFFSET     (USERNAME_OFFSET + USERNAME_SIZE)
+#define ROW_SIZE         (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)
```

这意味着行记录的布局如下所示：

| 属性列    | 大小 (bytes)    | 偏移量 |
|-----------|-----------------|--------|
| id        | 4               | 0      |
| username  | 32              | 4      |
| email     | 255             | 36     |
| total     | 291             |        |

我们需要编写代码来实现数据在紧凑格式和普通格式之间的转换。

```diff
+void
+serialize_row(Row *source, void *destination)
+{
+    char *dest = (char *) destination;
+    memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
+    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
+    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+}
+
+void
+deserialize_row(void *source, Row *destination)
+{
+    char *src = (char *) source;
+    memcpy(&(destination->id), src + ID_OFFSET, ID_SIZE);
+    memcpy(&(destination->username), src + USERNAME_OFFSET, USERNAME_SIZE);
+    memcpy(&(destination->email), src + EMAIL_OFFSET, EMAIL_SIZE);
+}
```

接着，我们需要一个 `Table` 结构来管理页面并跟踪表中的行记录信息：

```diff
+#define PAGE_SIZE        4096
+#define TABLE_MAX_PAGES  100
+#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
+#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)
+
+struct Table_t
+{
+    void       *pages[TABLE_MAX_PAGES];
+    uint32_t    num_rows;
+};
+typedef struct Table_t Table;
```

我们使用 4 KB 作为我们页面的大小，这是因为它正好与大多数计算机架构中的虚拟内存系统的页面大小相同。这就意味着我们数据库中的一个页面与操作系统中使用的一个页面相对应。操作系统将会把它们视为一个整体进行换入换出而不会将其打散。

我们将页面数量限制为 100。当使用树形结构时，数据库表的大小则将受到文件大小的限制。（不过，我们仍然会限制能在内存中保留几个页面。）

行记录不应该出现跨页的情况。因为两个页面在磁盘上相邻并不代表其在内存中也相邻，这个假设使得行记录的读写更容易。

接下来就是我们如何在确定行记录应该在内存的那个区域进行读写。

```diff
+void *
+row_slot(Table *table, uint32_t row_num)
+{
+    uint32_t   page_num = row_num / ROWS_PER_PAGE;
+    void      *page = table->pages[page_num];
+
+    if (!page) {
+        /* Allocate memory only when we try to access page */
+        page = table->pages[page_num] = malloc(PAGE_SIZE);
+    }
+
+    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
+    uint32_t    byte_offset = row_offset * ROW_SIZE;
+    return (char *) page + byte_offset;
+}
```

现在，我们修改 `execute_statement` 来读写表结构：

```diff
-void
-execute_statement(Statement *statement)
+ExecuteResult
+execute_insert(Statement *statement, Table *table)
+{
+    if (table->num_rows >= TABLE_MAX_ROWS) {
+        return EXECUTE_TABLE_FULL;
+    }
+
+    Row *row_to_insert = &statement->row_to_insert;
+    serialize_row(row_to_insert, row_slot(table, table->num_rows));
+    table->num_rows += 1;
+
+    return EXECUTE_SUCCESS;
+}
+
+ExecuteResult
+execute_select(Statement *statement, Table *table)
+{
+    Row row;
+    for (uint32_t i = 0; i < table->num_rows; i++) {
+        deserialize_row(row_slot(table, i), &row);
+        print_row(&row);
+    }
+
+    return EXECUTE_SUCCESS;
+}
+
+ExecuteResult
+execute_statement(Statement *statement, Table *table)
 {
     switch (statement->type) {
     case STATEMENT_INSERT:
-        printf("This is where we would do an insert.\n");
-        break;
+        return execute_insert(statement, table);
     case STATEMENT_SELECT:
-        printf("This is where we would do a select.\n");
-        break;
+        return execute_select(statement, table);
     }
+
+    return EXECUTE_UNKNOWN_STMT;
 }
```

最后，我们需要初始化表并且处理一些错误情况：

```diff
+Table *
+new_table()
+{
+    Table *table = malloc(sizeof(Table));
+    table->num_rows = 0;
+
+    return table;
 }

 int
 main(int argc, char *argv[])
 {
+    Table *table = new_table();
     InputBuffer *input_buffer = new_input_buffer();

     while (true) {
@@ -135,15 +211,58 @@ main(int argc, char *argv[])
         switch (prepare_statement(input_buffer, &statement)) {
         case PREPARE_SUCCESS:
             break;
+        case PREPARE_SYNTAX_ERROR:
+            printf("Syntax error. Could not parse statement.\n");
+            continue;
         case PREPARE_UNRECOGNIZED_STATEMENT:
             printf("Unrecognized keyword at start of '%s'.\n",
                    input_buffer->buffer);
             continue;
         }

-        execute_statement(&statement);
-        printf("Executed.\n");
+        switch (execute_statement(&statement, table)) {
+        case EXECUTE_SUCCESS:
+            printf("Executed.\n");
+            break;
+        case EXECUTE_TABLE_FULL:
+            printf("Error: Table full.\n");
+            break;
+        }
     }

     return 0;
 }
```

通过上述的修改，我们现在可向数据库中插入数据了！

```command-line
$ ./db
db > insert 1 cstack foo@bar.com
Executed.
db > insert 2 bob bob@example.com
Executed.
db > select
(1, cstack, foo@bar.com)
(2, bob, bob@example.com)
Executed.
db > insert foo bar 1
Syntax error. Could not parse statement.
db > .exit
```

现在时编写测试的时候了，主要有以下原因：

* 我们计划大幅改变存储表格的数据结构，测试会发现一些问题。
* 有几种边界情况我们没有测试到（例如，填满表的时候）。

我们将在下一章解决这些问题。目前，整个文件的变化如下所示：

``` diff
diff --git a/db.c b/db.c
index 7c9222f..cac52f6 100644
--- a/db.c
+++ b/db.c
@@ -2,6 +2,7 @@
 #include <stdlib.h>
 #include <string.h>
 #include <stdbool.h>
+#include <stdint.h>
 
 struct InputBuffer_t
 {
@@ -11,6 +12,14 @@ struct InputBuffer_t
 };
 typedef struct InputBuffer_t InputBuffer;
 
+enum ExecuteResult_t
+{
+    EXECUTE_SUCCESS,
+    EXECUTE_TABLE_FULL,
+    EXECUTE_UNKNOWN_STMT
+};
+typedef enum ExecuteResult_t ExecuteResult;
+
 enum MetaCommandResult_t
 {
     META_COMMAND_SUCCESS,
@@ -21,6 +30,7 @@ typedef enum MetaCommandResult_t MetaCommandResult;
 enum PrepareResult_t
 {
     PREPARE_SUCCESS,
+    PREPARE_SYNTAX_ERROR,
     PREPARE_UNRECOGNIZED_STATEMENT
 };
 typedef enum PrepareResult_t PrepareResult;
@@ -32,12 +42,67 @@ enum StatementType_t
 };
 typedef enum StatementType_t StatementType;
 
+#define COLUMN_USERNAME_SIZE    32
+#define COLUMN_EMAIL_SIZE       255
+struct Row_t
+{
+    uint32_t    id;
+    char        username[COLUMN_USERNAME_SIZE];
+    char        email[COLUMN_EMAIL_SIZE];
+};
+typedef struct Row_t Row;
+
 struct Statement_t
 {
     StatementType type;
+    Row           row_to_insert;  /* Only used by insert statement */
 };
 typedef struct Statement_t Statement;
 
+#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)
+
+#define ID_SIZE          size_of_attribute(Row, id)
+#define USERNAME_SIZE    size_of_attribute(Row, username)
+#define EMAIL_SIZE       size_of_attribute(Row, email)
+#define ID_OFFSET        0
+#define USERNAME_OFFSET  (ID_OFFSET + ID_SIZE)
+#define EMAIL_OFFSET     (USERNAME_OFFSET + USERNAME_SIZE)
+#define ROW_SIZE         (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)
+
+#define PAGE_SIZE        4096
+#define TABLE_MAX_PAGES  100
+#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
+#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)
+
+struct Table_t
+{
+    void       *pages[TABLE_MAX_PAGES];
+    uint32_t    num_rows;
+};
+typedef struct Table_t Table;
+
+
+void print_row(Row *row);
+InputBuffer *new_input_buffer();
+void print_prompt();
+void read_input(InputBuffer *input_buffer);
+MetaCommandResult do_meta_command(InputBuffer *input_buffer);
+PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
+ExecuteResult execute_insert(Statement *statement, Table *table);
+ExecuteResult execute_select(Statement *statement, Table *table);
+ExecuteResult execute_statement(Statement *statement, Table *table);
+Table *new_table();
+void serialize_row(Row *source, void *destination);
+void deserialize_row(void *source, Row *destination);
+void *row_slot(Table *table, uint32_t row_num);
+
+
+void
+print_row(Row *row)
+{
+    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
+}
+
 InputBuffer *
 new_input_buffer()
 {
@@ -89,6 +154,13 @@ prepare_statement(InputBuffer *input_buffer, Statement *statement)
 {
     if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
         statement->type = STATEMENT_INSERT;
+        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s",
+                                   &(statement->row_to_insert.id),
+                                   statement->row_to_insert.username,
+                                   statement->row_to_insert.email);
+        if (args_assigned < 3) {
+            return PREPARE_SYNTAX_ERROR;
+        }
         return PREPARE_SUCCESS;
     }
     if (strncmp(input_buffer->buffer, "select", 6) == 0) {
@@ -99,22 +171,58 @@ prepare_statement(InputBuffer *input_buffer, Statement *statement)
     return PREPARE_UNRECOGNIZED_STATEMENT;
 }
 
-void
-execute_statement(Statement *statement)
+ExecuteResult
+execute_insert(Statement *statement, Table *table)
+{
+    if (table->num_rows >= TABLE_MAX_ROWS) {
+        return EXECUTE_TABLE_FULL;
+    }
+
+    Row *row_to_insert = &statement->row_to_insert;
+    serialize_row(row_to_insert, row_slot(table, table->num_rows));
+    table->num_rows += 1;
+
+    return EXECUTE_SUCCESS;
+}
+
+ExecuteResult
+execute_select(Statement *statement, Table *table)
+{
+    Row row;
+    for (uint32_t i = 0; i < table->num_rows; i++) {
+        deserialize_row(row_slot(table, i), &row);
+        print_row(&row);
+    }
+
+    return EXECUTE_SUCCESS;
+}
+
+ExecuteResult
+execute_statement(Statement *statement, Table *table)
 {
     switch (statement->type) {
     case STATEMENT_INSERT:
-        printf("This is where we would do an insert.\n");
-        break;
+        return execute_insert(statement, table);
     case STATEMENT_SELECT:
-        printf("This is where we would do a select.\n");
-        break;
+        return execute_select(statement, table);
     }
+
+    return EXECUTE_UNKNOWN_STMT;
+}
+
+Table *
+new_table()
+{
+    Table *table = (Table *)malloc(sizeof(Table));
+    table->num_rows = 0;
+
+    return table;
 }
 
 int
 main(int argc, char *argv[])
 {
+    Table *table = new_table();
     InputBuffer *input_buffer = new_input_buffer();
 
     while (true) {
@@ -135,15 +243,58 @@ main(int argc, char *argv[])
         switch (prepare_statement(input_buffer, &statement)) {
         case PREPARE_SUCCESS:
             break;
+        case PREPARE_SYNTAX_ERROR:
+            printf("Syntax error. Could not parse statement.\n");
+            continue;
         case PREPARE_UNRECOGNIZED_STATEMENT:
             printf("Unrecognized keyword at start of '%s'.\n",
                    input_buffer->buffer);
             continue;
         }
 
-        execute_statement(&statement);
-        printf("Executed.\n");
+        switch (execute_statement(&statement, table)) {
+        case EXECUTE_SUCCESS:
+            printf("Executed.\n");
+            break;
+        case EXECUTE_TABLE_FULL:
+            printf("Error: Table full.\n");
+            break;
+        }
     }
 
     return 0;
 }
+
+void
+serialize_row(Row *source, void *destination)
+{
+    char *dest = (char *) destination;
+    memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
+    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
+    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+}
+
+void
+deserialize_row(void *source, Row *destination)
+{
+    char *src = (char *) source;
+    memcpy(&(destination->id), src + ID_OFFSET, ID_SIZE);
+    memcpy(&(destination->username), src + USERNAME_OFFSET, USERNAME_SIZE);
+    memcpy(&(destination->email), src + EMAIL_OFFSET, EMAIL_SIZE);
+}
+
+void *
+row_slot(Table *table, uint32_t row_num)
+{
+    uint32_t   page_num = row_num / ROWS_PER_PAGE;
+    void      *page = table->pages[page_num];
+
+    if (!page) {
+        /* Allocate memory only when we try to access page */
+        page = table->pages[page_num] = malloc(PAGE_SIZE);
+    }
+
+    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
+    uint32_t    byte_offset = row_offset * ROW_SIZE;
+    return (char *) page + byte_offset;
+}
```

__备注：__

1. 原文没有添加前置声明，我在文件开始增加了前置声明。
2. 函数定义的顺序可能与原文不一致。

