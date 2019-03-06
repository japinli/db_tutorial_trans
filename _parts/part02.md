---
title: 02 - 最简单的 SQL 编译器和虚拟机
date: 2019-03-06
---

我们将从头创建 sqlite 的复制版。Sqlite 的前端是一个 SQL 编译器，它解析字符串 (SQL 语句) 并输出字节码 (bytecode) 的中间结果。

该字节码将被传入到虚拟机并被执行。

{% include image.html url="assets/images/arch2.gif" description="SQLite 架构 (https://www.sqlite.org/arch.html)" %}

我们将数据库的执行划分为两个步骤主要有以下好处：

- 降低每个部分的复杂度 (例如，虚拟机不需要担心语法错误)；
- 允许将通用的查询编译为字节码并缓存起来从而提高新能。

考虑到这一点，现在我们来重构 `main` 函数并支持两个新的关键字：

```diff
 int
 main(int argc, char *argv[])
 {
@@ -56,11 +121,28 @@ main(int argc, char *argv[])
         print_prompt();
         read_input(input_buffer);

-        if (strcmp(input_buffer->buffer, ".exit") == 0) {
-            exit(EXIT_SUCCESS);
-        } else {
-            printf("Unrecognized command '%s'.\n", input_buffer->buffer);
+        if (input_buffer->buffer[0] == '.') {
+            switch (do_meta_command(input_buffer)) {
+            case META_COMMAND_SUCCESS:
+                continue;
+            case META_COMMAND_UNRECOGNIZED_COMMAND:
+                printf("Unrecognized command '%s'\n", input_buffer->buffer);
+                continue;
+            }
         }
+
+        Statement statement;
+        switch (prepare_statement(input_buffer, &statement)) {
+        case PREPARE_SUCCESS:
+            break;
+        case PREPARE_UNRECOGNIZED_STATEMENT:
+            printf("Unrecognized keyword at start of '%s'.\n",
+                   input_buffer->buffer);
+            continue;
+        }
+
+        execute_statement(&statement);
+        printf("Executed.\n");
     }

     return 0;
```

形如 `.exit` 的非 SQL (Non-SQL) 语句被称为元命令 (meta-commands)。它们均以点打头，因此我们检测元命令并在一个独立的函数中去处理它。

紧接着，我们添加一个步骤用于将用户输入转换为内部的语句。这是我们基于 sqlite 前端的一个简化版本。

最后，我们将准备好的语句传递给 `execute_statement` 函数。该函数最终将成为我们的虚拟机部分。

需要注意的是我们新增的函数返回枚举类型用于表示成功或失败：

```c
enum MetaCommandResult_t
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
};
typedef enum MetaCommandResult_t MetaCommandResult;

enum PrepareResult_t
{
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
};
typedef enum PrepareResult_t PrepareResult;
```

不能识别的语句？这有点类似异常。但是[异常机制比较糟糕](https://www.youtube.com/watch?v=EVhCUSgNbzo)（同时，C 语言不支持异常)，因此在实际中我们使用枚举类型的返回码来标识。如果我们在 switch 语句中没有处理某些枚举类型，C 编译器将会告警，这可以提醒我们处理所有的枚举类型。我们将在后续添加更多的返回码。

函数 `do_meta_command` 仅仅是现有功能的一个封装，它将在后续中添加更多的命令处理：

```c
MetaCommandResult do_meta_command(InputBuffer *input_buffer) {
	if (strcmp(input_buffer->buffer, ".exit") == 0) {
		exit(EXIT_SUCCESS);
	} else {
		return META_COMMAND_UNRECOGNIZED_COMMAND;
	}
}
```

目前我们的 "prepared statement" 仅包含两个可能的值。随着我们往后进行，他将包含更多的值：

```c
enum StatementType_t
{
	STATEMENT_INSERT,
	STATEMENT_SELECT
};
typedef enum StatementType_t StatementType;

struct Statement_t
{
	StatementType type;
};
typedef struct Statement_t Statement;
```

`prepare_statement` (我们的 SQL 编译器) 目前并不能识别 SQL 语句，它只能解析两个基本的单词：

```c
PrepareResult
prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}
```

注意到我们使用了 `strncmp` 函数来比较 "insert"，这是因为关键字 "insert" 后面将会紧跟数据。（例如， `insert 1 cstack foo@bar.com` ）

最后，`execute_statement` 函数的内容如下所示：

``` c
void
execute_statement(Statement *statement)
{
    switch (statement->type) {
    case STATEMENT_INSERT:
        printf("This is where we would do an insert.\n");
        break;
    case STATEMENT_SELECT:
        printf("This is where we would do a select.\n");
        break;
    }
}
```

上述函数由于目前没有作任何事情因此不需要返回错误码。经过上面的重构，我们的数据库现在可以解析两个关键字了！

``` command-line
~ ./db
db > insert foo bar
This is where we would do an insert.
Executed.
db > delete foo
Unrecognized keyword at start of 'delete foo'.
db > select
This is where we would do a select.
Executed.
db > .tables
Unrecognized command '.tables'
db > .exit
~
```

我们的数据库骨架已基本成型，如果是能存储数据那将更完美了，不是吗？在接下来的章节中，我们将实现 `insert` 和 `select` 命令，并创建世上最糟糕的数据存储。下面是本章源码的 diff 内容：

``` diff
diff --git a/db.c b/db.c
index 46b6a5c..7c9222f 100644
--- a/db.c
+++ b/db.c
@@ -11,6 +11,33 @@ struct InputBuffer_t
 };
 typedef struct InputBuffer_t InputBuffer;

+enum MetaCommandResult_t
+{
+    META_COMMAND_SUCCESS,
+    META_COMMAND_UNRECOGNIZED_COMMAND
+};
+typedef enum MetaCommandResult_t MetaCommandResult;
+
+enum PrepareResult_t
+{
+    PREPARE_SUCCESS,
+    PREPARE_UNRECOGNIZED_STATEMENT
+};
+typedef enum PrepareResult_t PrepareResult;
+
+enum StatementType_t
+{
+    STATEMENT_INSERT,
+    STATEMENT_SELECT
+};
+typedef enum StatementType_t StatementType;
+
+struct Statement_t
+{
+    StatementType type;
+};
+typedef struct Statement_t Statement;
+
 InputBuffer *
 new_input_buffer()
 {
@@ -47,6 +74,44 @@ read_input(InputBuffer *input_buffer)
     input_buffer->buffer[bytes_read - 1] = 0;
 }

+MetaCommandResult
+do_meta_command(InputBuffer *input_buffer)
+{
+    if (strcmp(input_buffer->buffer, ".exit") == 0) {
+        exit(EXIT_SUCCESS);
+    }
+
+    return META_COMMAND_UNRECOGNIZED_COMMAND;
+}
+
+PrepareResult
+prepare_statement(InputBuffer *input_buffer, Statement *statement)
+{
+    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
+        statement->type = STATEMENT_INSERT;
+        return PREPARE_SUCCESS;
+    }
+    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
+        statement->type = STATEMENT_SELECT;
+        return PREPARE_SUCCESS;
+    }
+
+    return PREPARE_UNRECOGNIZED_STATEMENT;
+}
+
+void
+execute_statement(Statement *statement)
+{
+    switch (statement->type) {
+    case STATEMENT_INSERT:
+        printf("This is where we would do an insert.\n");
+        break;
+    case STATEMENT_SELECT:
+        printf("This is where we would do a select.\n");
+        break;
+    }
+}
+
 int
 main(int argc, char *argv[])
 {
@@ -56,11 +121,28 @@ main(int argc, char *argv[])
         print_prompt();
         read_input(input_buffer);

-        if (strcmp(input_buffer->buffer, ".exit") == 0) {
-            exit(EXIT_SUCCESS);
-        } else {
-            printf("Unrecognized command '%s'.\n", input_buffer->buffer);
+        if (input_buffer->buffer[0] == '.') {
+            switch (do_meta_command(input_buffer)) {
+            case META_COMMAND_SUCCESS:
+                continue;
+            case META_COMMAND_UNRECOGNIZED_COMMAND:
+                printf("Unrecognized command '%s'\n", input_buffer->buffer);
+                continue;
+            }
         }
+
+        Statement statement;
+        switch (prepare_statement(input_buffer, &statement)) {
+        case PREPARE_SUCCESS:
+            break;
+        case PREPARE_UNRECOGNIZED_STATEMENT:
+            printf("Unrecognized keyword at start of '%s'.\n",
+                   input_buffer->buffer);
+            continue;
+        }
+
+        execute_statement(&statement);
+        printf("Executed.\n");
     }

     return 0;
```


__备注：__

1. 我不清楚原文作者使用的 diff 格式，因此在翻译时我采用 `git diff` 来生成的差异。
2. 我修改了原作者部分代码的缩进格式，因此可能会与原文有所出入。
