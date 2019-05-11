---
title: 04 - 测试用例
date: 2019-05-11
---

我们现在已经可以向数据库中插入数据并且可以查询所有的数据了。现在我们需要针对我们已实现的功能进行测试。

由于我对 [rspec][] 比较熟悉并且它的语法易于阅读，因此我将采用它来编写测试代码。

我将定义一个简短的辅助函数用于向数据库发送一系列命令，随后通过断言的方式来判断输出结果：

``` ruby
describe 'database' do
  def run_script(commands)
    raw_output = nil
    IO.popen("./db", "r+") do |pipe|
      commands.each do |command|
        pipe.puts command
      end

      pipe.close_write

      # Read entire output
      raw_output = pipe.gets(nil)
    end
    raw_output.split("\n")
  end

  it 'inserts and retreives a row' do
    result = run_script([
      "insert 1 user1 person1@example.com",
      "select",
      ".exit",
    ])
    expect(result).to match_array([
      "db > Executed.",
      "db > (1, user1, person1@example.com)",
      "Executed.",
      "db > ",
    ])
  end
end
```

这个简单的测试将我们存入的数据从数据库中取出来，并且它能正常工作。

``` command-line
bundle exec rspec
.

Finished in 0.00871 seconds (files took 0.09506 seconds to load)
1 example, 0 failures
```

现在，我们可以测试大批数据的插入了：

``` ruby
it 'prints error message when table is full' do
  script = (1..1401).map do |i|
    "insert #{i} user#{i} person#{i}@example.com"
  end
  script << ".exit"
  result = run_script(script)
  expect(result[-2]).to eq('db > Error: Table full.')
end
```

太好了，它能正常工作！我们的数据库目前只能存储 1400 行数据，这是因为我们定义的最大的页面数量为 100，而每个页面最多容纳 14 行记录。

如果仔细阅读代码我们可以发现在处理文本域时，可能会发现一些问题。我们可以通过下面的测试用例进行测试：

``` ruby
it 'allows inserting strings that are the maximum length' do
  long_username = "a"*32
  long_email = "a"*255
  script = [
    "insert 1 #{long_username} #{long_email}",
    "select",
    ".exit",
  ]
  result = run_script(script)
  expect(result).to match_array([
    "db > Executed.",
    "db > (1, #{long_username}, #{long_email})",
    "Executed.",
    "db > ",
  ])
end
```

测试失败了！

``` ruby
Failures:

  1) database allows inserting strings that are the maximum length
     Failure/Error: raw_output.split("\n")

     ArgumentError:
       invalid byte sequence in UTF-8
     # ./spec/main_spec.rb:14:in `split'
     # ./spec/main_spec.rb:14:in `run_script'
     # ./spec/main_spec.rb:48:in `block (2 levels) in <top (required)>'
```

如果我们采用手工测试，当我们试图打印行记录时，将会看到一些奇怪的字符(此处缩短了字符串长度)：

```command-line
db > insert 1 aaaaa... aaaaa...
Executed.
db > select
(1, aaaaa...aaa\�, aaaaa...aaa\�)
Executed.
db >
```

这发生了什么？如果您仔细阅读 Row 的定义，您将发现我们在为 username 和 email 分别分配了固定长度为 32 和 255 的存储空间。但是，[C 语言的字符串][]类型总是以空字符结尾，然而，我们并没有为其分配空间。解决方案则是为其额外分配一个字节的存储空间。

``` diff
 struct Row_t
 {
     uint32_t    id;
-    char        username[COLUMN_USERNAME_SIZE];
-    char        email[COLUMN_EMAIL_SIZE];
+    char        username[COLUMN_USERNAME_SIZE + 1];
+    char        email[COLUMN_EMAIL_SIZE + 1];
 };
 typedef struct Row_t Row;
```

当然，这解决了问题：

```ruby
 bundle exec rspec
...

Finished in 0.0188 seconds (files took 0.08516 seconds to load)
3 examples, 0 failures
```

此外，我们不应该插入 username 或 email 长度超过其存储空间大小的数据。测试用例如下所示：

``` ruby
it 'prints error message if strings are too long' do
  long_username = "a"*33
  long_email = "a"*256
  script = [
    "insert 1 #{long_username} #{long_email}",
    "select",
    ".exit",
  ]
  result = run_script(script)
  expect(result).to match_array([
    "db > String is too long.",
    "db > Executed.",
    "db > ",
  ])
end
```

为了实现这个功能，我们需要更新我们的解析器。我们目前使用的是 [scanf()][] 实现的：

``` c
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
```

但是 【scanf 也有自身的一些缺陷][]。如果读取的数据超过了缓冲区的大小，那么它将导致缓冲区溢出，并且将数据写入到我们不期望写入的地方。因此，在我们将字符串拷贝到 Row 结构中时，我们需要验证其长度。为了实现这点，我们通过空格字符来划分将用户输入。

我将使用 [strtok()] 函数来划分字符串，我认为这是最简单有效的方式：

``` diff
@@ -153,15 +154,7 @@ PrepareResult
 prepare_statement(InputBuffer *input_buffer, Statement *statement)
 {
     if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
-        statement->type = STATEMENT_INSERT;
-        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s",
-                                   &(statement->row_to_insert.id),
-                                   statement->row_to_insert.username,
-                                   statement->row_to_insert.email);
-        if (args_assigned < 3) {
-            return PREPARE_SYNTAX_ERROR;
-        }
-        return PREPARE_SUCCESS;
+        return prepare_insert(input_buffer, statement);
     }
     if (strncmp(input_buffer->buffer, "select", 6) == 0) {
         statement->type = STATEMENT_SELECT;
@@ -298,3 +291,32 @@ row_slot(Table *table, uint32_t row_num)
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
 }
+
+PrepareResult
+prepare_insert(InputBuffer *input_buffer, Statement *statement)
+{
+    statement->type = STATEMENT_INSERT;
+
+    char *keyword = strtok(input_buffer->buffer, " ");
+    char *id_string = strtok(NULL, " ");
+    char *username = strtok(NULL, " ");
+    char *email = strtok(NULL, " ");
+
+    if (id_string == NULL || username == NULL || email == NULL) {
+        return PREPARE_SYNTAX_ERROR;
+    }
+
+    int id = atoi(id_string);
+    if (strlen(username) > COLUMN_USERNAME_SIZE) {
+        return PREPARE_STRING_TOO_LONG;
+    }
+    if (strlen(email) > COLUMN_EMAIL_SIZE) {
+        return PREPARE_STRING_TOO_LONG;
+    }
+
+    statement->row_to_insert.id = id;
+    strcpy(statement->row_to_insert.username, username);
+    strcpy(statement->row_to_insert.email, email);
+
+    return PREPARE_SUCCESS;
+}
```

我们通过持续的在输入缓冲区上调用 strtok 函数将用户输入转换为子字符串（通过空格字符分割）。strtok 函数会在分隔符处插入一个空字符并返回子字符串的开始地址。

接着，我们通过调用 [strlen()] 函数来判断文本值是否过长。

最后，我们可以像处理其他错误一样处理字符串过长的问题：

``` diff
@@ -30,6 +30,7 @@ typedef enum MetaCommandResult_t MetaCommandResult;
 enum PrepareResult_t
 {
     PREPARE_SUCCESS,
+    PREPARE_STRING_TOO_LONG,
     PREPARE_SYNTAX_ERROR,
     PREPARE_UNRECOGNIZED_STATEMENT
 };
```

``` diff
@@ -243,6 +237,9 @@ main(int argc, char *argv[])
         switch (prepare_statement(input_buffer, &statement)) {
         case PREPARE_SUCCESS:
             break;
+        case PREPARE_STRING_TOO_LONG:
+            printf("String is too long.\n");
+            continue;
         case PREPARE_SYNTAX_ERROR:
             printf("Syntax error. Could not parse statement.\n");
             continue;
```

这使得我们的测试用例得以通过。

``` ruby
bundle exec rspec
....

Finished in 0.02284 seconds (files took 0.116 seconds to load)
4 examples, 0 failures
```

接下来，我们还要处理一个错误情况：

``` ruby
it 'prints an error message if id is negative' do
  script = [
    "insert -1 cstack foo@bar.com",
    "select",
    ".exit",
  ]
  result = run_script(script)
  expect(result).to match_array([
    "db > ID must be positive.",
    "db > Executed.",
    "db > ",
  ])
end
```

``` diff
 enum PrepareResult_t
 {
     PREPARE_SUCCESS,
+    PREPARE_NEGATIVE_ID,
     PREPARE_STRING_TOO_LONG,
     PREPARE_SYNTAX_ERROR,
     PREPARE_UNRECOGNIZED_STATEMENT
 };
```

``` diff
switch (prepare_statement(input_buffer, &statement)) {
         case PREPARE_SUCCESS:
             break;
+        case PREPARE_NEGATIVE_ID:
+            printf("ID must be positive.\n");
+            continue;
         case PREPARE_STRING_TOO_LONG:
             printf("String is too long.\n");
             continue;
         case PREPARE_SYNTAX_ERROR:
             printf("Syntax error. Could not parse statement.\n");
             continue;
```

``` diff
     int id = atoi(id_string);
+    if (id < 0) {
+        return PREPARE_NEGATIVE_ID;
+    }
     if (strlen(username) > COLUMN_USERNAME_SIZE) {
         return PREPARE_STRING_TOO_LONG;
     }
```

现在我们已经有了足够的测试用例了。接下来我们将引入一个重要的特性：持久性！我们将保存数据库的内容到文件中并从文件中读取出来。

下面是完整的 diff 比较：

``` diff
diff --git a/db.c b/db.c
index cac52f6..420f6c5 100644
--- a/db.c
+++ b/db.c
@@ -30,6 +30,8 @@ typedef enum MetaCommandResult_t MetaCommandResult;
 enum PrepareResult_t
 {
     PREPARE_SUCCESS,
+    PREPARE_NEGATIVE_ID,
+    PREPARE_STRING_TOO_LONG,
     PREPARE_SYNTAX_ERROR,
     PREPARE_UNRECOGNIZED_STATEMENT
 };
@@ -47,8 +49,8 @@ typedef enum StatementType_t StatementType;
 struct Row_t
 {
     uint32_t    id;
-    char        username[COLUMN_USERNAME_SIZE];
-    char        email[COLUMN_EMAIL_SIZE];
+    char        username[COLUMN_USERNAME_SIZE + 1];
+    char        email[COLUMN_EMAIL_SIZE + 1];
 };
 typedef struct Row_t Row;
 
@@ -88,6 +90,7 @@ void print_prompt();
 void read_input(InputBuffer *input_buffer);
 MetaCommandResult do_meta_command(InputBuffer *input_buffer);
 PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
+PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
 ExecuteResult execute_insert(Statement *statement, Table *table);
 ExecuteResult execute_select(Statement *statement, Table *table);
 ExecuteResult execute_statement(Statement *statement, Table *table);
@@ -153,15 +156,7 @@ PrepareResult
 prepare_statement(InputBuffer *input_buffer, Statement *statement)
 {
     if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
-        statement->type = STATEMENT_INSERT;
-        int args_assigned = sscanf(input_buffer->buffer, "insert %d %s %s",
-                                   &(statement->row_to_insert.id),
-                                   statement->row_to_insert.username,
-                                   statement->row_to_insert.email);
-        if (args_assigned < 3) {
-            return PREPARE_SYNTAX_ERROR;
-        }
-        return PREPARE_SUCCESS;
+        return prepare_insert(input_buffer, statement);
     }
     if (strncmp(input_buffer->buffer, "select", 6) == 0) {
         statement->type = STATEMENT_SELECT;
@@ -243,6 +238,12 @@ main(int argc, char *argv[])
         switch (prepare_statement(input_buffer, &statement)) {
         case PREPARE_SUCCESS:
             break;
+        case PREPARE_NEGATIVE_ID:
+            printf("ID must be positive.\n");
+            continue;
+        case PREPARE_STRING_TOO_LONG:
+            printf("String is too long.\n");
+            continue;
         case PREPARE_SYNTAX_ERROR:
             printf("Syntax error. Could not parse statement.\n");
             continue;
@@ -298,3 +299,35 @@ row_slot(Table *table, uint32_t row_num)
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
 }
+
+PrepareResult
+prepare_insert(InputBuffer *input_buffer, Statement *statement)
+{
+    statement->type = STATEMENT_INSERT;
+
+    char *keyword = strtok(input_buffer->buffer, " ");
+    char *id_string = strtok(NULL, " ");
+    char *username = strtok(NULL, " ");
+    char *email = strtok(NULL, " ");
+
+    if (id_string == NULL || username == NULL || email == NULL) {
+        return PREPARE_SYNTAX_ERROR;
+    }
+
+    int id = atoi(id_string);
+    if (id < 0) {
+        return PREPARE_NEGATIVE_ID;
+    }
+    if (strlen(username) > COLUMN_USERNAME_SIZE) {
+        return PREPARE_STRING_TOO_LONG;
+    }
+    if (strlen(email) > COLUMN_EMAIL_SIZE) {
+        return PREPARE_STRING_TOO_LONG;
+    }
+
+    statement->row_to_insert.id = id;
+    strcpy(statement->row_to_insert.username, username);
+    strcpy(statement->row_to_insert.email, email);
+
+    return PREPARE_SUCCESS;
+}
```

以及我们添加的测试：

``` diff
+describe 'database' do
+  def run_script(commands)
+    raw_output = nil
+    IO.popen("./db", "r+") do |pipe|
+      commands.each do |command|
+        pipe.puts command
+      end
+
+      pipe.close_write
+
+      # Read entire output
+      raw_output = pipe.gets(nil)
+    end
+    raw_output.split("\n")
+  end
+
+  it 'inserts and retreives a row' do
+       result = run_script([
+         "insert 1 user1 person1@example.com",
+         "select",
+         ".exit",
+       ])
+       expect(result).to match_array([
+         "db > Executed.",
+         "db > (1, user1, person1@example.com)",
+         "Executed.",
+         "db > ",
+       ])
+  end
+
+  it 'prints error message when table is full' do
+    script = (1..1401).map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".exit"
+    result = run_script(script)
+    expect(result[-2]).to eq('db > Error: Table full.')
+  end
+
+  it 'allows inserting strings that are the maximum length' do
+    long_username = "a"*32
+    long_email = "a"*255
+    script = [
+      "insert 1 #{long_username} #{long_email}",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > (1, #{long_username}, #{long_email})",
+      "Executed.",
+      "db > ",
+    ])
+  end
+
+  it 'prints error message if strings are too long' do
+    long_username = "a"*33
+    long_email = "a"*256
+    script = [
+      "insert 1 #{long_username} #{long_email}",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > String is too long.",
+      "db > Executed.",
+      "db > ",
+    ])
+  end
+
+  it 'prints an error message if id is negative' do
+    script = [
+      "insert -1 cstack foo@bar.com",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > ID must be positive.",
+      "db > Executed.",
+      "db > ",
+    ])
+  end
+
+end
```

----------------

[rspec]: http://rspec.info/
[C 语言的字符串]: http://www.cprogramming.com/tutorial/c/lesson9.html
[scanf()]: https://linux.die.net/man/3/scanf
[scanf 也有自身的一些缺陷]: https://stackoverflow.com/questions/2430303/disadvantages-of-scanf
[strlen()]: http://www.cplusplus.com/reference/cstring/strlen/
