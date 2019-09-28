---
title: 05 - 数据持久化
date: 2019-09-07
---

> "Nothing in the world can take the place of persistence." – Calvin Coolidge

现在，只要我们保证程序一直运行，那么我们便可以向我们的数据库中插入数据并将其读取出来。但是，一旦程序停止后重新启动，我们的数据将会丢失。而我们想要的规范如下所示：

``` ruby
it 'keeps data after closing connection' do
  result1 = run_script([
    "insert 1 user1 person1@example.com",
    ".exit",
  ])
  expect(result1).to match_array([
    "db > Executed.",
    "db > ",
  ])
  result2 = run_script([
    "select",
    ".exit",
  ])
  expect(result2).to match_array([
    "db > (1, user1, person1@example.com)",
    "Executed.",
    "db > ",
  ])
end
```

同 SQLite 一样，我们将通过保存整个数据库到一个文件中来持久化记录。

我们已经将序列化的行记录保存在一个内存块中。为了实现持久化，我们可以简单地将这些内存块写入文件，并在下次程序启动时将它们载入内存。

为了使得它更为简单易懂，我们将抽象出一个页面管理器（pager）。我们只需要向页面管理器请求编号为 `x` 的页面，此时，页面管理器将返回一个内存块给我们。它首先在缓存（cache）中寻找。当缓存未命中时，它将从磁盘拷贝数据到内存中（通过读取数据库文件）。

{% include image.html url="assets/images/arch-part5.gif" description="我们的数据库与 SQLite 数据库对比" %}

页面管理器访问页面缓存和文件。表对象则通过页面管理器发出页面请求。

``` diff
@@ -76,9 +76,16 @@ typedef struct Statement_t Statement;
 #define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
 #define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)

+typedef struct Pager_t
+{
+    int       file_descriptor;
+    uint32_t  file_length;
+    void     *pages[TABLE_MAX_PAGES];
+} Pager;
+
 struct Table_t
 {
-    void       *pages[TABLE_MAX_PAGES];
+    Pager      *pager;
     uint32_t    num_rows;
 };
 typedef struct Table_t Table;
```

我将 `new_table()` 重命名为 `db_open()`，因为它现在具有打开数据库连接的效果。打开连接的意思是：

* 打开数据库文件；
* 初始化页面管理器（pager）数据结构；
* 初始化表数据结构。

``` diff
@@ -94,7 +101,7 @@ PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
 ExecuteResult execute_insert(Statement *statement, Table *table);
 ExecuteResult execute_select(Statement *statement, Table *table);
 ExecuteResult execute_statement(Statement *statement, Table *table);
-Table *new_table();
+Table *db_open(const char *filename);
 void serialize_row(Row *source, void *destination);
 void deserialize_row(void *source, Row *destination);
 void *row_slot(Table *table, uint32_t row_num);
@@ -206,10 +213,14 @@ execute_statement(Statement *statement, Table *table)
 }

 Table *
-new_table()
+db_open(const char *filename)
 {
-    Table *table = (Table *)malloc(sizeof(Table));
-    table->num_rows = 0;
+    Pager    *pager = pager_open(filename);
+    uint32_t  num_rows = pager->file_length / ROW_SIZE;
+    Table    *table = (Table *)malloc(sizeof(Table));
+
+    table->pager = pager;
+    table->num_rows = num_rows;

     return table;
 }
```

`db_open()` 调用 `page_open()` 函数来打开数据库文件并且跟踪文件的大小。它将页面缓存初始化为空。

``` diff
@@ -331,3 +346,35 @@ prepare_insert(InputBuffer *input_buffer, Statement *statement)

     return PREPARE_SUCCESS;
 }
+
+Pager *
+pager_open(const char *filename)
+{
+    int        fd;
+    off_t      file_length;
+    uint32_t   i;
+    Pager     *pager;
+
+    fd = open(filename,
+              O_RDWR |      /* Read/Write mode */
+              O_CREAT,      /* Create file if it does not exist */
+              S_IWUSR |     /* User write permission */
+              S_IRUSR);     /* User Read permission */
+
+    if (fd == -1) {
+        printf("Unable to open file\n");
+        exit(EXIT_FAILURE);
+    }
+
+    file_length = lseek(fd, 0, SEEK_END);
+
+    pager = malloc(sizeof(Pager));
+    pager->file_descriptor = fd;
+    pager->file_length = file_length;
+
+    for (i = 0; i < TABLE_MAX_PAGES; i++) {
+        pager->pages[i] = NULL;
+    }
+
+    return pager;
+}
```

遵循我们新提出的抽象，我们需要修改获取行记录的逻辑。

``` diff
@@ -288,13 +303,7 @@ void *
 row_slot(Table *table, uint32_t row_num)
 {
     uint32_t   page_num = row_num / ROWS_PER_PAGE;
-    void      *page = table->pages[page_num];
-
-    if (!page) {
-        /* Allocate memory only when we try to access page */
-        page = table->pages[page_num] = malloc(PAGE_SIZE);
-    }
-
+    void      *page = get_page(table->pager, page_num);
     uint32_t    row_offset = row_num % ROWS_PER_PAGE;
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
```

函数 `get_page()` 需要处理缓存未命中的逻辑。我们假设页面在数据库文件中一个页面接着一个页面存储：页面编号 0 的偏移位置为 0，页面编号 1 的偏移位置为 4096，页面编号 2 的偏移位置为 8192，以此类推。如果请求的页面位于文件的边界之外，那么它应该是空白的页面，因此，我们仅在内存中创建一个页面并返回。这个新建的页面将在刷新缓存到磁盘时被添加到数据文件中。

``` diff
+void *
+get_page(Pager *pager, uint32_t page_num)
+{
+    if (page_num > TABLE_MAX_PAGES) {
+        printf("Tried to fetch page number out of bounds. %d > %d\n",
+               page_num, TABLE_MAX_PAGES);
+        exit(EXIT_FAILURE);
+    }
+
+    if (pager->pages[page_num] == NULL) {
+        /* Cache miss. Allocate memory and load from file. */
+        void      *page = malloc(PAGE_SIZE);
+        uint32_t   num_pages = pager->file_length / PAGE_SIZE;
+
+        /* We might save a partial page at the end of the file. */
+        if (pager->file_length % PAGE_SIZE) {
+            num_pages += 1;
+        }
+
+        if (page_num < num_pages) {
+            ssize_t bytes_read;
+            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+            bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
+            if (bytes_read == -1) {
+                printf("Error reading file: %d\n", errno);
+                exit(EXIT_FAILURE);
+            }
+        }
+
+        pager->pages[page_num] = page;
+    }
+
+    return pager->pages[page_num];
+}
```

现在，我们需要等待数据库关闭连接，并将缓存数据刷新到磁盘数据库文件。当用户退出程序时，我们调用 `db_close()` 函数执行下面的动作：

* 刷新页面缓存数据到磁盘；
* 关闭数据库文件；
* 释放页面管理器以及表结构的内存空间。

``` diff
+void
+db_close(Table *table)
+{
+    Pager     *pager = table->pager;
+    uint32_t   num_full_pages = table->num_rows / ROWS_PER_PAGE;
+    uint32_t   num_additional_rows;
+    uint32_t   i;
+    int        result;
+
+    for (i = 0; i < num_full_pages; i++) {
+        if (pager->pages[i] == NULL) {
+            continue;
+        }
+
+        pager_flush(pager, i, PAGE_SIZE);
+        free(pager->pages[i]);
+        pager->pages[i] = NULL;
+    }
+
+    /*
+     * There may be a partial page to write to the end of the file.
+     * This should not be needed after we switch to a B-tree.
+     */
+    num_additional_rows = table->num_rows % ROWS_PER_PAGE;
+    if (num_additional_rows > 0) {
+        uint32_t page_num = num_full_pages;
+        if (pager->pages[page_num] != NULL) {
+            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
+            free(pager->pages[page_num]);
+            pager->pages[page_num] = NULL;
+        }
+    }
+
+    result = close(pager->file_descriptor);
+    if (result == -1) {
+        printf("Error closing db file.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    for (i = 0; i < TABLE_MAX_PAGES; i++) {
+        void *page = pager->pages[i];
+        if (page) {
+            free(page);
+            pager->pages[i] = NULL;
+        }
+    }
+    free(pager);
+    free(table);
+}
```

In our current design, the length of the file encodes how many rows are in the database, so we need to write a partial page at the end of the file. That’s why pager_flush() takes both a page number and a size. It’s not the greatest design, but it will go away pretty quickly when we start implementing the B-tree.

在我们目前的设计中，文件的长度包含了数据库中存储了多少记录，因此，我们需要将最后未能填充满的页面追加到文件末尾。这就是为什么 `pager_flush()` 函数包含页面数量以及大小的原因。这并不是最好的设计，我们将在后面使用 B-树替换它。

``` diff
+void
+pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
+{
+    off_t      offset;
+    ssize_t  bytes_written;
+    if (pager->pages[page_num] == NULL) {
+        printf("Tried to flush null page\n");
+        exit(EXIT_FAILURE);
+    }
+
+    offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+
+    if (offset == -1) {
+        printf("Error seeking: %d\n", errno);
+        exit(EXIT_FAILURE);
+    }
+
+    bytes_written =
+        write(pager->file_descriptor, pager->pages[page_num], size);
+
+    if (bytes_written == -1) {
+        printf("Error writing: %d\n", errno);
+        exit(EXIT_FAILURE);
+    }
+}
```

最后，我们需要在命令行参数中给出数据库文件名。此外，别忘了还要在 `do_meta_command` 函数中添加额外的参数：

``` diff
 int
 main(int argc, char *argv[])
 {
-    Table *table = new_table();
-    InputBuffer *input_buffer = new_input_buffer();
+    char           *filename;
+    Table          *table;
+    InputBuffer    *input_buffer;
+
+    if (argc < 2) {
+        printf("Must supply a database filename.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    filename = argv[1];
+    table = db_open(filename);
+    input_buffer = new_input_buffer();

     while (true) {
         print_prompt();
         read_input(input_buffer);

         if (input_buffer->buffer[0] == '.') {
-            switch (do_meta_command(input_buffer)) {
+            switch (do_meta_command(input_buffer, table)) {
             case META_COMMAND_SUCCESS:
                 continue;
             case META_COMMAND_UNRECOGNIZED_COMMAND:
```

经过上述的改造，我们便可以将用户数据保存在文件中，当程序退出后在此运行时，我们仍可以获取到上次的插入的数据：

``` shell
~ ./db mydb.db
db > insert 1 cstack foo@bar.com
Executed.
db > insert 2 voltorb volty@example.com
Executed.
db > .exit
~
~ ./db mydb.db
db > select
(1, cstack, foo@bar.com)
(2, voltorb, volty@example.com)
Executed.
db > .exit
~
```

我们可以使用十六进制编辑器来查看当前文件的存储格式，这里给出 vim 的查看方式：

``` shell
vim mydb.db
:%! xxd
```

{% include image.html url="assets/images/file-format.png" description="当前文件格式" %}

文件中的前 4 个字节为第一条记录的 `id`（因为我们将其存储在 `uint32_t` 类型中，所以是 4 个字节）。它采用小端序进行存储，因此最低有效为在前（`01`），后面紧跟高位字节 （`00 00 00`）。我们使用 `memcpy()` 将 `Row` 结构中的字节拷贝到页面缓存中，这就意味着它们以小端序存储。这取决于我编译程序时的机器属性。如果我想要在我的加上写入数据文件，随后在大端序的机器上读取，那么我们需要修改 `serialize_row()` 和 `deserialize_row()` 函数来保证它们以相同的字节序进行读写。

接下来的 33 个字节用来存储 `username` 并以空字符结尾。显然，字符串 "cstack" ASCII 十六进制的是 `63 73 74 61 63 6b`，后跟一个空字符（`00`）。 其余部分则未使用。

接下来的 256 个字节用来存储 `email`，其方式与 `username` 相同。这里我们看到在空字符结尾有一些随机字符。这是由于我们没有初始化 `Row` 结构内存导致的。我们将 `email` 缓冲中的 256 个字节全部拷贝到文件中。当分配内存时，它可能包含一些随机字符，由于我们使用空字符作为结束标识，因此它对程序的行为没有任何影响。

__注意：__ 如果您想要确保所有的字节都被初始化，您可以在 `serialize_row` 函数中拷贝 `username` 和 `email` 时使用 `strncpy` 来取代 `memcpy`，例如：

``` diff
@@ -354,8 +354,8 @@ serialize_row(Row *source, void *destination)
 {
     char *dest = (char *) destination;
     memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
-    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
-    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+    strncpy(dest + USERNAME_OFFSET, source->username, USERNAME_SIZE);
+    strncpy(dest + EMAIL_OFFSET, source->email, EMAIL_SIZE);
 }
```

## 总结

现在我们以及支持数据持久化了。虽然这不是最好的。例如，当您杀掉进程而不是使用 `.exit` 退出程序时，您仍然会丢失数据。此外，我们会讲所有页面写回到磁盘中，即使这些页面从磁盘读取后从未发生更改。我们将在后续解决这些问题。

下次，我们将介绍游标，这将使实现 B-树更加容易。

## 完整的 Diff

``` diff
diff --git a/db.c b/db.c
index 420f6c5..c13021c 100644
--- a/db.c
+++ b/db.c
@@ -3,6 +3,10 @@
 #include <string.h>
 #include <stdbool.h>
 #include <stdint.h>
+#include <errno.h>
+
+#include <unistd.h>
+#include <fcntl.h>
 
 struct InputBuffer_t
 {
@@ -76,9 +80,16 @@ typedef struct Statement_t Statement;
 #define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
 #define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)
 
+typedef struct Pager_t
+{
+    int       file_descriptor;
+    uint32_t  file_length;
+    void     *pages[TABLE_MAX_PAGES];
+} Pager;
+
 struct Table_t
 {
-    void       *pages[TABLE_MAX_PAGES];
+    Pager      *pager;
     uint32_t    num_rows;
 };
 typedef struct Table_t Table;
@@ -88,17 +99,21 @@ void print_row(Row *row);
 InputBuffer *new_input_buffer();
 void print_prompt();
 void read_input(InputBuffer *input_buffer);
-MetaCommandResult do_meta_command(InputBuffer *input_buffer);
+MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table);
 PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
 PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
 ExecuteResult execute_insert(Statement *statement, Table *table);
 ExecuteResult execute_select(Statement *statement, Table *table);
 ExecuteResult execute_statement(Statement *statement, Table *table);
-Table *new_table();
+Table *db_open(const char *filename);
+void db_close(Table *table);
 void serialize_row(Row *source, void *destination);
 void deserialize_row(void *source, Row *destination);
 void *row_slot(Table *table, uint32_t row_num);
 
+Pager *pager_open(const char *filename);
+void *get_page(Pager *pager, uint32_t page_num);
+void pager_flush(Pager* pager, uint32_t page_num, uint32_t size);
 
 void
 print_row(Row *row)
@@ -143,9 +158,10 @@ read_input(InputBuffer *input_buffer)
 }
 
 MetaCommandResult
-do_meta_command(InputBuffer *input_buffer)
+do_meta_command(InputBuffer *input_buffer, Table *table)
 {
     if (strcmp(input_buffer->buffer, ".exit") == 0) {
+        db_close(table);
         exit(EXIT_SUCCESS);
     }
 
@@ -206,26 +222,90 @@ execute_statement(Statement *statement, Table *table)
 }
 
 Table *
-new_table()
+db_open(const char *filename)
 {
-    Table *table = (Table *)malloc(sizeof(Table));
-    table->num_rows = 0;
+    Pager    *pager = pager_open(filename);
+    uint32_t  num_rows = pager->file_length / ROW_SIZE;
+    Table    *table = (Table *)malloc(sizeof(Table));
+
+    table->pager = pager;
+    table->num_rows = num_rows;
 
     return table;
 }
 
+void
+db_close(Table *table)
+{
+    Pager     *pager = table->pager;
+    uint32_t   num_full_pages = table->num_rows / ROWS_PER_PAGE;
+    uint32_t   num_additional_rows;
+    uint32_t   i;
+    int        result;
+
+    for (i = 0; i < num_full_pages; i++) {
+        if (pager->pages[i] == NULL) {
+            continue;
+        }
+
+        pager_flush(pager, i, PAGE_SIZE);
+        free(pager->pages[i]);
+        pager->pages[i] = NULL;
+    }
+
+    /*
+     * There may be a partial page to write to the end of the file.
+     * This should not be needed after we switch to a B-tree.
+     */
+    num_additional_rows = table->num_rows % ROWS_PER_PAGE;
+    if (num_additional_rows > 0) {
+        uint32_t page_num = num_full_pages;
+        if (pager->pages[page_num] != NULL) {
+            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
+            free(pager->pages[page_num]);
+            pager->pages[page_num] = NULL;
+        }
+    }
+
+    result = close(pager->file_descriptor);
+    if (result == -1) {
+        printf("Error closing db file.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    for (i = 0; i < TABLE_MAX_PAGES; i++) {
+        void *page = pager->pages[i];
+        if (page) {
+            free(page);
+            pager->pages[i] = NULL;
+        }
+    }
+    free(pager);
+    free(table);
+}
+
 int
 main(int argc, char *argv[])
 {
-    Table *table = new_table();
-    InputBuffer *input_buffer = new_input_buffer();
+    char           *filename;
+    Table          *table;
+    InputBuffer    *input_buffer;
+
+    if (argc < 2) {
+        printf("Must supply a database filename.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    filename = argv[1];
+    table = db_open(filename);
+    input_buffer = new_input_buffer();
 
     while (true) {
         print_prompt();
         read_input(input_buffer);
 
         if (input_buffer->buffer[0] == '.') {
-            switch (do_meta_command(input_buffer)) {
+            switch (do_meta_command(input_buffer, table)) {
             case META_COMMAND_SUCCESS:
                 continue;
             case META_COMMAND_UNRECOGNIZED_COMMAND:
@@ -260,6 +340,9 @@ main(int argc, char *argv[])
         case EXECUTE_TABLE_FULL:
             printf("Error: Table full.\n");
             break;
+        case EXECUTE_UNKNOWN_STMT:
+            printf("Error: Unknown statement.\n");
+            break;
         }
     }
 
@@ -271,8 +354,8 @@ serialize_row(Row *source, void *destination)
 {
     char *dest = (char *) destination;
     memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
-    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
-    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
+    strncpy(dest + USERNAME_OFFSET, source->username, USERNAME_SIZE);
+    strncpy(dest + EMAIL_OFFSET, source->email, EMAIL_SIZE);
 }
 
 void
@@ -288,13 +371,7 @@ void *
 row_slot(Table *table, uint32_t row_num)
 {
     uint32_t   page_num = row_num / ROWS_PER_PAGE;
-    void      *page = table->pages[page_num];
-
-    if (!page) {
-        /* Allocate memory only when we try to access page */
-        page = table->pages[page_num] = malloc(PAGE_SIZE);
-    }
-
+    void      *page = get_page(table->pager, page_num);
     uint32_t    row_offset = row_num % ROWS_PER_PAGE;
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
@@ -331,3 +408,96 @@ prepare_insert(InputBuffer *input_buffer, Statement *statement)
 
     return PREPARE_SUCCESS;
 }
+
+Pager *
+pager_open(const char *filename)
+{
+    int        fd;
+    off_t      file_length;
+    uint32_t   i;
+    Pager     *pager;
+
+    fd = open(filename,
+              O_RDWR |      /* Read/Write mode */
+              O_CREAT,      /* Create file if it does not exist */
+              S_IWUSR |     /* User write permission */
+              S_IRUSR);     /* User Read permission */
+
+    if (fd == -1) {
+        printf("Unable to open file\n");
+        exit(EXIT_FAILURE);
+    }
+
+    file_length = lseek(fd, 0, SEEK_END);
+
+    pager = malloc(sizeof(Pager));
+    pager->file_descriptor = fd;
+    pager->file_length = file_length;
+
+    for (i = 0; i < TABLE_MAX_PAGES; i++) {
+        pager->pages[i] = NULL;
+    }
+
+    return pager;
+}
+
+void *
+get_page(Pager *pager, uint32_t page_num)
+{
+    if (page_num > TABLE_MAX_PAGES) {
+        printf("Tried to fetch page number out of bounds. %d > %d\n",
+               page_num, TABLE_MAX_PAGES);
+        exit(EXIT_FAILURE);
+    }
+
+    if (pager->pages[page_num] == NULL) {
+        /* Cache miss. Allocate memory and load from file. */
+        void      *page = malloc(PAGE_SIZE);
+        uint32_t   num_pages = pager->file_length / PAGE_SIZE;
+
+        /* We might save a partial page at the end of the file. */
+        if (pager->file_length % PAGE_SIZE) {
+            num_pages += 1;
+        }
+
+        if (page_num < num_pages) {
+            ssize_t bytes_read;
+            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+            bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
+            if (bytes_read == -1) {
+                printf("Error reading file: %d\n", errno);
+                exit(EXIT_FAILURE);
+            }
+        }
+
+        pager->pages[page_num] = page;
+    }
+
+    return pager->pages[page_num];
+}
+
+void
+pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
+{
+    off_t	offset;
+    ssize_t  bytes_written;
+    if (pager->pages[page_num] == NULL) {
+        printf("Tried to flush null page\n");
+        exit(EXIT_FAILURE);
+    }
+
+    offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
+
+    if (offset == -1) {
+        printf("Error seeking: %d\n", errno);
+        exit(EXIT_FAILURE);
+    }
+
+    bytes_written =
+        write(pager->file_descriptor, pager->pages[page_num], size);
+
+    if (bytes_written == -1) {
+        printf("Error writing: %d\n", errno);
+        exit(EXIT_FAILURE);
+    }
+}
```

以及测试文件的 Diff

``` diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index 0a5579a..5e13329 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -1,7 +1,11 @@
 describe 'database' do
+  before do
+    `rm -rf test.db`
+  end
+
   def run_script(commands)
     raw_output = nil
-    IO.popen("./db", "r+") do |pipe|
+    IO.popen("./db test.db", "r+") do |pipe|
       commands.each do |command|
         pipe.puts command
       end
@@ -28,6 +32,27 @@ describe 'database' do
        ])
   end

+  it 'keeps data after closing connection' do
+    result1 = run_script([
+      "insert 1 user1 person1@example.com",
+      ".exit",
+    ])
+    expect(result1).to match_array([
+      "db > Executed.",
+      "db > ",
+    ])
+
+    result2 = run_script([
+      "select",
+      ".exit",
+    ])
+    expect(result2).to match_array([
+      "db > (1, user1, person1@example.com)",
+      "Executed.",
+      "db > ",
+    ])
+  end
+
   it 'prints error message when table is full' do
     script = (1..1401).map do |i|
       "insert #{i} user#{i} person#{i}@example.com"
```
