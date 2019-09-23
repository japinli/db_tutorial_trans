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
+            pager_flush(pager, page_num, PAGE_SIZE);
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
