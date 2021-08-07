---
title: 08 - B-Tree 叶子节点格式
date: 2021-07-26
---

我们正在改变我们的表的格式，从一个无分类的行数组到一个B-Tree。这是一个相当大的变化，需要多篇文章来实现。在这篇文章的最后，我们将定义 的布局，并支持将键/值对插入到单节点树中。但首先，让我们回顾一下切换到树形结构的原因。

## 替换表格式

在目前的格式下，每页只存储行（没有元数据），所以它的空间效率相当高。插入的速度也很快，因为我们只是追加到最后。然而，要找到某一行，只能通过扫描整个表来完成。如果我们想删除某一行，我们必须通过移动它后面的每一行来填补这个漏洞。

如果我们将表存储为一个数组，但保持行按 id 排序，我们可以使用二进制搜索来找到一个特定的 id。然而，插入的速度会很慢，因为我们必须移动大量的行来腾出空间。

相反，我们要用一个树形结构。树中的每个节点可以包含数量不等的行，所以我们必须在每个节点中存储一些信息，以跟踪它包含多少行。另外，还有所有不存储任何行的内部节点的存储开销。作为对较大数据库文件的交换，我们得到了快速插入、删除和查询。

|            | 未排序的行数组 | 排序后的行数组 | 树节点             |
|------------|----------------|----------------|--------------------|
| 页面包含   | 仅数据         | 仅数据         | 元数据、主键和数据 |
| 每页的行数 | 多             | 多             | 少                 |
| 插入       | O(1)           | O(n)           | O(log(n))          |
| 删除       | O(n)           | O(n)           | O(log(n))          |
| 按 id 查找 | O(n)           | O(log(n))      | O(log(n))          |

## 节点头格式

叶子结点和内部结点有不同的布局。让我们做一个枚举来跟踪节点的类型。

```diff
+typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;
```

每个节点将对应于一个页面。内部节点将通过存储子节点的页码来指向其子节点。Btree 向页面管理器询问一个特定的页号，并得到一个进入页缓存的指针。指针进入页面缓存。页面按照页码的顺序一个接一个地存储在数据库文件中。

节点需要在页面开头部分存储一些元数据。每个节点将存储节点的类型、是否为根节点，以及指向父节点的指针（以允许查找节点的兄弟节点）。我为每个字段的大小和长度定义了一个常量：

```diff
+/*
+ * Common Node Header Layout
+ */
+const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
+const uint32_t NODE_TYPE_OFFSET = 0;
+const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
+const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
+const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
+const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
+const uint8_t COMMON_NODE_HEADER_SIZE =
+    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;
```

## 叶子节点格式

除了这些常见的页面头信息外，叶子节点还需要存储它们所包含的 `cells` 数量。一个 `cell` 是一个键值对。

```diff
+/*
+ * Leaf Node Header Layout
+ */
+const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE =
+    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
```

叶子节点的主体是由 `cells` 组成的数组。每个 `cell` 是一个键，后面是一个值（一个序列化的行）。

```diff
+/*
+ * Leaf Node Body Layout
+ */
+const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_KEY_OFFSET = 0;
+const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
+const uint32_t LEAF_NODE_VALUE_OFFSET =
+    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
+const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
+const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_MAX_CELLS =
+    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
```

基于这些常量，下面是一个叶子节点的布局的样子：

{% include image.html url="assets/images/leaf-node-format.png" description="叶子节点布局" %}

在页面头部信息中，每个布尔值使用一个整型值，这有点浪费空间，但是这使得编写访问这些值的代码更容易。

还需要注意的是，在页面的最后存储空间的浪费。我们尽可能多的在页面头部之后存储 `cells`，但是剩余的空间不能容纳整个 `cell` 时，我们把它留空，以避免在节点之间分割 `cell`。

## 访问叶子节点字段

访问键、值和元数据的代码都涉及到我们刚才定义的常量的指针运算。

```diff
+uint32_t *
+leaf_node_num_cells(void *node)
+{
+    return node + LEAF_NODE_NUM_CELLS_OFFSET;
+}
+
+void *
+leaf_node_cell(void *node, uint32_t cell_num)
+{
+    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
+}
+
+uint32_t *
+leaf_node_key(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num);
+}
+
+void *
+leaf_node_value(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
+}
+
+void
+initialize_leaf_node(void *node)
+{
+    *leaf_node_num_cells(node) = 0;
+}
```

这些方法返回一个指向相关值的指针，所以它们既可以作为一个获取器，也可以作为一个设置器使用。

## 修改页面管理器和表对象

每一个节点都会恰好占据一个页面，即使它并不完整。这就意味着我们的页面管理器不支持读写部分页面。

```diff
 void
-pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
+pager_flush(Pager* pager, uint32_t page_num)
 {
     off_t      offset;
     ssize_t  bytes_written;
@@ -515,7 +555,7 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
     }

     bytes_written =
-        write(pager->file_descriptor, pager->pages[page_num], size);
+        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

     if (bytes_written == -1) {
         printf("Error writing: %d\n", errno);
```

```diff
@@ -258,35 +298,20 @@ void
 db_close(Table *table)
 {
     Pager     *pager = table->pager;
-    uint32_t   num_full_pages = table->num_rows / ROWS_PER_PAGE;
     uint32_t   num_additional_rows;
     uint32_t   i;
     int        result;

-    for (i = 0; i < num_full_pages; i++) {
+    for (i = 0; i < pager->num_pages; i++) {
         if (pager->pages[i] == NULL) {
             continue;
         }

-        pager_flush(pager, i, PAGE_SIZE);
+        pager_flush(pager, i);
         free(pager->pages[i]);
         pager->pages[i] = NULL;
     }

-    /*
-     * There may be a partial page to write to the end of the file.
-     * This should not be needed after we switch to a B-tree.
-     */
-    num_additional_rows = table->num_rows % ROWS_PER_PAGE;
-    if (num_additional_rows > 0) {
-        uint32_t page_num = num_full_pages;
-        if (pager->pages[page_num] != NULL) {
-            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
-            free(pager->pages[page_num]);
-            pager->pages[page_num] = NULL;
-        }
-    }
-
     result = close(pager->file_descriptor);
     if (result == -1) {
         printf("Error closing db file.\n");
```

现在，在我们的数据库中存储页数比存储行数更有意义。页数应该与页面管理器 `pager` 对象相关联，而不是与表相关联，因为它是数据使用的页数，而不是一个特定的表。一个 btree 是由它的根节点的页数来识别的，所以表对象需要跟踪它。

```diff
@@ -77,20 +77,19 @@ typedef struct Statement_t Statement;

 #define PAGE_SIZE        4096
 #define TABLE_MAX_PAGES  100
-#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
-#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)

 typedef struct Pager_t
 {
     int       file_descriptor;
     uint32_t  file_length;
+    uint32_t  num_pages;
     void     *pages[TABLE_MAX_PAGES];
 } Pager;

 struct Table_t
 {
     Pager      *pager;
-    uint32_t    num_rows;
+    uint32_t    root_page_num;
 };
 typedef struct Table_t Table;
```

```diff
@@ -492,13 +516,17 @@ get_page(Pager *pager, uint32_t page_num)
         }

         pager->pages[page_num] = page;
+
+        if (page_num >= pager->num_pages) {
+            pager->num_pages = page_num + 1;
+        }
     }

     return pager->pages[page_num];
 }
```

```diff
@@ -454,6 +478,12 @@ pager_open(const char *filename)
     pager = malloc(sizeof(Pager));
     pager->file_descriptor = fd;
     pager->file_length = file_length;
+    pager->num_pages = (file_length / PAGE_SIZE);
+
+    if (file_length % PAGE_SIZE != 0) {
+        printf("Db file is not a whole number of pages. Corrupt file.\n");
+        exit(EXIT_FAILURE);
+    }

     for (i = 0; i < TABLE_MAX_PAGES; i++) {
         pager->pages[i] = NULL;
```

## 修改游标对象

游标代表了表中的一个位置。当我们的表是一个简单的行数组时，我们可以通过行号来访问一条记录。现在它是一棵树，我们可以通过节点的页码和该节点中 `cell` 的编号来确定一个位置。

```diff
 typedef struct {
     Table    *table;
-    uint32_t  row_num;
+    uint32_t  page_num;
+    uint32_t  cell_num;
     bool      end_of_table; /* Indicates a position one past the last element */
 } Cursor;
```

```diff
 Cursor *
 table_start(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));

     cursor->table = table;
-    cursor->row_num = 0;
-    cursor->end_of_table = (table->num_rows == 0);
+    cursor->page_num = table->root_page_num;
+    cursor->cell_num = 0;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->end_of_table = (num_cells == 0);

     return cursor;
 }
```

```diff
 Cursor *
 table_end(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));

     cursor->table = table;
-    cursor->row_num = table->num_rows;
+    cursor->page_num = table->root_page_num;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->cell_num = num_cells;
     cursor->end_of_table = true;

     return cursor;
```

```diff
 void *
 cursor_value(Cursor *cursor)
 {
-    uint32_t   row_num = cursor->row_num;
-    uint32_t   page_num = row_num / ROWS_PER_PAGE;
+    uint32_t   page_num = cursor->page_num;
     void      *page = get_page(cursor->table->pager, page_num);
-    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
-    uint32_t    byte_offset = row_offset * ROW_SIZE;
-    return (char *) page + byte_offset;
+
+    return leaf_node_value(page, cursor->cell_num);
 }
```

```diff
 void
 cursor_advance(Cursor *cursor)
 {
-    cursor->row_num += 1;
-    if (cursor->row_num >= cursor->table->num_rows) {
+    uint32_t  page_num = cursor->page_num;
+    void     *node = get_page(cursor->table->pager, page_num);
+
+    cursor->cell_num += 1;
+    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
         cursor->end_of_table = true;
     }
 }
```

## 插入叶子节点

在本文中，我们只打算实现足以得到一个单节点的树。回顾一下上一篇文章，树开始时是一个空的叶子节点：

{% include image.html url="assets/images/btree1.png" description="空 btree" %}

可以添加键/值对，直到叶节点满：

{% include image.html url="assets/images/btree2.png" description="一个节点的 btree" %}

当我们第一次打开数据库时，数据库文件是空的，所以我们将第 `0` 页初始化为一个空的叶子节点（根节点）。

<!-- 已修改 -->
```diff
@@ -245,11 +285,16 @@ Table *
 db_open(const char *filename)
 {
     Pager    *pager = pager_open(filename);
-    uint32_t  num_rows = pager->file_length / ROW_SIZE;
     Table    *table = (Table *)malloc(sizeof(Table));

     table->pager = pager;
-    table->num_rows = num_rows;
+    table->root_page_num = 0;
+
+    if (pager->num_pages == 0) {
+        /* New database file. Initialize page 0 as leaf node. */
+        void *root_node = get_page(pager, 0);
+        initialize_leaf_node(root_node);
+    }

     return table;
 }
```

接下来我们将制作一个函数，用于将键/值对插入到叶子节点中。它将接受一个光标作为输入，以表示这对键值对被插入的位置。

```diff
+void
+leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
+{
+    void *node = get_page(cursor->table->pager, cursor->page_num);
+
+    uint32_t num_cells = *leaf_node_num_cells(node);
+    if (num_cells >= LEAF_NODE_MAX_CELLS) {
+        /* Node full */
+        printf("Need to implement splitting a leaf node.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    if (cursor->cell_num < num_cells) {
+        /* Make room for new cell */
+        uint32_t i;
+        for (i = num_cells; i > cursor->cell_num; i--) {
+            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
+                   LEAF_NODE_CELL_SIZE);
+        }
+    }
+
+    *(leaf_node_num_cells(node)) += 1;
+    *(leaf_node_key(node, cursor->cell_num)) = key;
+    serialize_row(value, leaf_node_value(node, cursor->cell_num));
+}
```

我们目前还没有实现页面拆分，所以，如果节点已满，我们将报错。接下来我们将 `cell` 先右移动一个空格，为新的 `cell` 腾出空间。然后，我们把新的键值写进空位。

由于我们假设树只有一个节点，我们的 `execute_insert()` 函数只需要调用这个辅助方法：

```diff
 ExecuteResult
 execute_insert(Statement *statement, Table *table)
 {
-    if (table->num_rows >= TABLE_MAX_ROWS) {
+    Row    *row_to_insert;
+    Cursor *cursor;
+    void   *node = get_page(table->pager, table->root_page_num);
+
+    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
         return EXECUTE_TABLE_FULL;
     }

-    Row *row_to_insert = &statement->row_to_insert;
-    Cursor *cursor = table_end(table);
+    row_to_insert = &statement->row_to_insert;
+    cursor = table_end(table);

-    serialize_row(row_to_insert, cursor_value(cursor));
-    table->num_rows += 1;
+    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

     free(cursor);
```

有了这些变化，我们的数据库应该像以前一样工作了 只是现在它更快地返回一个 "表满 "的错误，因为我们还不能分割根节点。

叶子节点能容纳多少行呢？

## 打印常量的命令

我正在添加一个新的元命令，以打印出一些感兴趣的常量。


```diff
+void print_constants() {
+  printf("ROW_SIZE: %d\n", ROW_SIZE);
+  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
+  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
+  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
+  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
+}
```

```diff
@@ -174,6 +226,10 @@ do_meta_command(InputBuffer *input_buffer, Table *table)
     if (strcmp(input_buffer->buffer, ".exit") == 0) {
         db_close(table);
         exit(EXIT_SUCCESS);
+    } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
+        printf("Constants:\n");
+        print_constants();
+        return META_COMMAND_SUCCESS;
     }

     return META_COMMAND_UNRECOGNIZED_COMMAND;
```

我还添加了一个测试，这样当这些常量发生变化时，我们就会得到提醒。

```diff
+  it 'prints constants' do
+    script = [
+      ".constants",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Constants:",
+      "ROW_SIZE: 293",
+      "COMMON_NODE_HEADER_SIZE: 6",
+      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_CELL_SIZE: 297",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_MAX_CELLS: 13",
+      "db > ",
+    ])
+  end
```

因此，我们的表格现在可以容纳 13 行!

## 树的可视化

为了帮助调试和可视化，我还增加了一个元命令来打印出 btree 的表示。

```diff
+void
+print_leaf_node(void* node)
+{
+    uint32_t i;
+    uint32_t num_cells = *leaf_node_num_cells(node);
+
+    printf("leaf (size %d)\n", num_cells);
+    for (i = 0; i < num_cells; i++) {
+        uint32_t key = *leaf_node_key(node, i);
+        printf("  - %d : %d\n", i, key);
+    }
+}
```

```diff
     if (strcmp(input_buffer->buffer, ".exit") == 0) {
         db_close(table);
         exit(EXIT_SUCCESS);
+    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
+        printf("Tree:\n");
+        print_leaf_node(get_page(table->pager, 0));
+        return META_COMMAND_SUCCESS;
     } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
         printf("Constants:\n");
         print_constants();
         return META_COMMAND_SUCCESS;
     }
```

下面是测试。

```diff
+  it 'allows printing out the structure of a one-node btree' do
+    script = [3, 1, 2].map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".btree"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Executed.",
+      "db > Executed.",
+      "db > Tree:",
+      "leaf (size 3)",
+      "  - 0 : 3",
+      "  - 1 : 1",
+      "  - 2 : 2",
+      "db > "
+    ])
+  end
```

我们还是没有按排序的顺序来存储行。你会注意到 `execute_insert()` 是在 `table_end()` 返回的位置插入叶子节点的。所以，行是按照它们被插入的顺序存储的，就像以前一样。

## 后续

这一切可能看起来像是一种退步。我们的数据库现在存储的行数比以前少了，而且我们仍然是以未排序的顺序存储行数。但是就像我一开始说的，这是一个很大的变化，重要的是要把它分成可管理的步骤。

在下一节中，我们将实现通过主键查找记录，并开始按排序顺序存储记录。

## 完整的 Diff

```diff
diff --git a/db.c b/db.c
index 9b4a206..38f8176 100644
--- a/db.c
+++ b/db.c
@@ -77,29 +77,63 @@ typedef struct Statement_t Statement;
 
 #define PAGE_SIZE        4096
 #define TABLE_MAX_PAGES  100
-#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
-#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)
 
 typedef struct Pager_t
 {
     int       file_descriptor;
     uint32_t  file_length;
+    uint32_t  num_pages;
     void     *pages[TABLE_MAX_PAGES];
 } Pager;
 
 struct Table_t
 {
     Pager      *pager;
-    uint32_t    num_rows;
+    uint32_t    root_page_num;
 };
 typedef struct Table_t Table;
 
 typedef struct {
     Table    *table;
-    uint32_t  row_num;
+    uint32_t  page_num;
+    uint32_t  cell_num;
     bool      end_of_table; /* Indicates a position one past the last element */
 } Cursor;
 
+typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;
+
+/*
+ * Common Node Header Layout
+ */
+const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
+const uint32_t NODE_TYPE_OFFSET = 0;
+const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
+const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
+const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
+const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
+const uint8_t COMMON_NODE_HEADER_SIZE =
+    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;
+
+/*
+ * Leaf Node Header Layout
+ */
+const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE =
+    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+
+/*
+ * Leaf Node Body Layout
+ */
+const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_KEY_OFFSET = 0;
+const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
+const uint32_t LEAF_NODE_VALUE_OFFSET =
+    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
+const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
+const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_MAX_CELLS =
+    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
 
 void print_row(Row *row);
 InputBuffer *new_input_buffer();
@@ -119,19 +153,50 @@ void *row_slot(Table *table, uint32_t row_num);
 
 Pager *pager_open(const char *filename);
 void *get_page(Pager *pager, uint32_t page_num);
-void pager_flush(Pager* pager, uint32_t page_num, uint32_t size);
+void pager_flush(Pager* pager, uint32_t page_num);
 
 Cursor *table_start(Table *table);
 Cursor *table_end(Table *table);
 void cursor_advance(Cursor *cursor);
 void *cursor_value(Cursor *cursor);
 
+uint32_t *leaf_node_num_cells(void *node);
+void *leaf_node_cell(void *node, uint32_t cell_num);
+uint32_t *leaf_node_key(void *node, uint32_t cell_num);
+void *leaf_node_value(void *node, uint32_t cell_num);
+void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
+void initialize_leaf_node(void *node);
+
 void
 print_row(Row *row)
 {
     printf("(%d, %s, %s)\n", row->id, row->username, row->email);
 }
 
+void
+print_constants()
+{
+    printf("ROW_SIZE: %lu\n", ROW_SIZE);
+    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
+    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
+    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
+    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
+    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
+}
+
+void
+print_leaf_node(void* node)
+{
+    uint32_t i;
+    uint32_t num_cells = *leaf_node_num_cells(node);
+
+    printf("leaf (size %d)\n", num_cells);
+    for (i = 0; i < num_cells; i++) {
+        uint32_t key = *leaf_node_key(node, i);
+        printf("  - %d : %d\n", i, key);
+    }
+}
+
 InputBuffer *
 new_input_buffer()
 {
@@ -174,6 +239,14 @@ do_meta_command(InputBuffer *input_buffer, Table *table)
     if (strcmp(input_buffer->buffer, ".exit") == 0) {
         db_close(table);
         exit(EXIT_SUCCESS);
+    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
+        printf("Tree:\n");
+        print_leaf_node(get_page(table->pager, 0));
+        return META_COMMAND_SUCCESS;
+    } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
+        printf("Constants:\n");
+        print_constants();
+        return META_COMMAND_SUCCESS;
     }
 
     return META_COMMAND_UNRECOGNIZED_COMMAND;
@@ -196,15 +269,18 @@ prepare_statement(InputBuffer *input_buffer, Statement *statement)
 ExecuteResult
 execute_insert(Statement *statement, Table *table)
 {
-    if (table->num_rows >= TABLE_MAX_ROWS) {
+    Row    *row_to_insert;
+    Cursor *cursor;
+    void   *node = get_page(table->pager, table->root_page_num);
+
+    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
         return EXECUTE_TABLE_FULL;
     }
 
-    Row *row_to_insert = &statement->row_to_insert;
-    Cursor *cursor = table_end(table);
+    row_to_insert = &statement->row_to_insert;
+    cursor = table_end(table);
 
-    serialize_row(row_to_insert, cursor_value(cursor));
-    table->num_rows += 1;
+    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
 
     free(cursor);
 
@@ -245,11 +321,16 @@ Table *
 db_open(const char *filename)
 {
     Pager    *pager = pager_open(filename);
-    uint32_t  num_rows = pager->file_length / ROW_SIZE;
     Table    *table = (Table *)malloc(sizeof(Table));
 
     table->pager = pager;
-    table->num_rows = num_rows;
+    table->root_page_num = 0;
+
+    if (pager->num_pages == 0) {
+        /* New database file. Initialize page 0 as leaf node. */
+        void *root_node = get_page(pager, 0);
+        initialize_leaf_node(root_node);
+    }
 
     return table;
 }
@@ -258,35 +339,20 @@ void
 db_close(Table *table)
 {
     Pager     *pager = table->pager;
-    uint32_t   num_full_pages = table->num_rows / ROWS_PER_PAGE;
     uint32_t   num_additional_rows;
     uint32_t   i;
     int        result;
 
-    for (i = 0; i < num_full_pages; i++) {
+    for (i = 0; i < pager->num_pages; i++) {
         if (pager->pages[i] == NULL) {
             continue;
         }
 
-        pager_flush(pager, i, PAGE_SIZE);
+        pager_flush(pager, i);
         free(pager->pages[i]);
         pager->pages[i] = NULL;
     }
 
-    /*
-     * There may be a partial page to write to the end of the file.
-     * This should not be needed after we switch to a B-tree.
-     */
-    num_additional_rows = table->num_rows % ROWS_PER_PAGE;
-    if (num_additional_rows > 0) {
-        uint32_t page_num = num_full_pages;
-        if (pager->pages[page_num] != NULL) {
-            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
-            free(pager->pages[page_num]);
-            pager->pages[page_num] = NULL;
-        }
-    }
-
     result = close(pager->file_descriptor);
     if (result == -1) {
         printf("Error closing db file.\n");
@@ -390,12 +456,10 @@ deserialize_row(void *source, Row *destination)
 void *
 cursor_value(Cursor *cursor)
 {
-    uint32_t   row_num = cursor->row_num;
-    uint32_t   page_num = row_num / ROWS_PER_PAGE;
+    uint32_t   page_num = cursor->page_num;
     void      *page = get_page(cursor->table->pager, page_num);
-    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
-    uint32_t    byte_offset = row_offset * ROW_SIZE;
-    return (char *) page + byte_offset;
+
+    return leaf_node_value(page, cursor->cell_num);
 }
 
 PrepareResult
@@ -454,6 +518,12 @@ pager_open(const char *filename)
     pager = malloc(sizeof(Pager));
     pager->file_descriptor = fd;
     pager->file_length = file_length;
+    pager->num_pages = (file_length / PAGE_SIZE);
+
+    if (file_length % PAGE_SIZE != 0) {
+        printf("Db file is not a whole number of pages. Corrupt file.\n");
+        exit(EXIT_FAILURE);
+    }
 
     for (i = 0; i < TABLE_MAX_PAGES; i++) {
         pager->pages[i] = NULL;
@@ -492,13 +562,17 @@ get_page(Pager *pager, uint32_t page_num)
         }
 
         pager->pages[page_num] = page;
+
+        if (page_num >= pager->num_pages) {
+            pager->num_pages = page_num + 1;
+        }
     }
 
     return pager->pages[page_num];
 }
 
 void
-pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
+pager_flush(Pager* pager, uint32_t page_num)
 {
     off_t	offset;
     ssize_t  bytes_written;
@@ -515,7 +589,7 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
     }
 
     bytes_written =
-        write(pager->file_descriptor, pager->pages[page_num], size);
+        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
 
     if (bytes_written == -1) {
         printf("Error writing: %d\n", errno);
@@ -526,11 +600,17 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
 Cursor *
 table_start(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
 
     cursor->table = table;
-    cursor->row_num = 0;
-    cursor->end_of_table = (table->num_rows == 0);
+    cursor->page_num = table->root_page_num;
+    cursor->cell_num = 0;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->end_of_table = (num_cells == 0);
 
     return cursor;
 }
@@ -538,10 +618,16 @@ table_start(Table *table)
 Cursor *
 table_end(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
 
     cursor->table = table;
-    cursor->row_num = table->num_rows;
+    cursor->page_num = table->root_page_num;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->cell_num = num_cells;
     cursor->end_of_table = true;
 
     return cursor;
@@ -550,8 +636,67 @@ table_end(Table *table)
 void
 cursor_advance(Cursor *cursor)
 {
-    cursor->row_num += 1;
-    if (cursor->row_num >= cursor->table->num_rows) {
+    uint32_t  page_num = cursor->page_num;
+    void     *node = get_page(cursor->table->pager, page_num);
+
+    cursor->cell_num += 1;
+    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
         cursor->end_of_table = true;
     }
 }
+
+uint32_t *
+leaf_node_num_cells(void *node)
+{
+    return node + LEAF_NODE_NUM_CELLS_OFFSET;
+}
+
+void *
+leaf_node_cell(void *node, uint32_t cell_num)
+{
+    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
+}
+
+uint32_t *
+leaf_node_key(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num);
+}
+
+void *
+leaf_node_value(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
+}
+
+void
+leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
+{
+    void *node = get_page(cursor->table->pager, cursor->page_num);
+
+    uint32_t num_cells = *leaf_node_num_cells(node);
+    if (num_cells >= LEAF_NODE_MAX_CELLS) {
+        /* Node full */
+        printf("Need to implement splitting a leaf node.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    if (cursor->cell_num < num_cells) {
+        /* Make room for new cell */
+        uint32_t i;
+        for (i = num_cells; i > cursor->cell_num; i--) {
+            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
+                   LEAF_NODE_CELL_SIZE);
+        }
+    }
+
+    *(leaf_node_num_cells(node)) += 1;
+    *(leaf_node_key(node, cursor->cell_num)) = key;
+    serialize_row(value, leaf_node_value(node, cursor->cell_num));
+}
+
+void
+initialize_leaf_node(void *node)
+{
+    *leaf_node_num_cells(node) = 0;
+}
```

测试用例。

```diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index 5e13329..652f9fa 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -109,4 +109,44 @@ describe 'database' do
     ])
   end
 
+  it 'prints constants' do
+    script = [
+      ".constants",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Constants:",
+      "ROW_SIZE: 293",
+      "COMMON_NODE_HEADER_SIZE: 6",
+      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_CELL_SIZE: 297",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_MAX_CELLS: 13",
+      "db > ",
+    ])
+  end
+
+  it 'allows printing out the structure of a one-node btree' do
+    script = [3, 1, 2].map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".btree"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Executed.",
+      "db > Executed.",
+      "db > Tree:",
+      "leaf (size 3)",
+      "  - 0 : 3",
+      "  - 1 : 1",
+      "  - 2 : 2",
+      "db > "
+    ])
+  end
+
 end
```

## 格式化后的完整 Diff

```diff
diff --git a/db.c b/db.c
index 9b4a206..f85211b 100644
--- a/db.c
+++ b/db.c
@@ -8,6 +8,8 @@
 #include <unistd.h>
 #include <fcntl.h>
 
+#define unused(expr) ((void) (expr))
+
 struct InputBuffer_t
 {
     char      *buffer;
@@ -77,61 +79,138 @@ typedef struct Statement_t Statement;
 
 #define PAGE_SIZE        4096
 #define TABLE_MAX_PAGES  100
-#define ROWS_PER_PAGE    (PAGE_SIZE / ROW_SIZE)
-#define TABLE_MAX_ROWS   (ROWS_PER_PAGE * TABLE_MAX_PAGES)
 
 typedef struct Pager_t
 {
     int       file_descriptor;
     uint32_t  file_length;
+    uint32_t  num_pages;
     void     *pages[TABLE_MAX_PAGES];
 } Pager;
 
 struct Table_t
 {
     Pager      *pager;
-    uint32_t    num_rows;
+    uint32_t    root_page_num;
 };
 typedef struct Table_t Table;
 
 typedef struct {
     Table    *table;
-    uint32_t  row_num;
+    uint32_t  page_num;
+    uint32_t  cell_num;
     bool      end_of_table; /* Indicates a position one past the last element */
 } Cursor;
 
+typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;
+
+/*
+ * Common Node Header Layout
+ */
+const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
+const uint32_t NODE_TYPE_OFFSET = 0;
+const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
+const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
+const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
+const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
+const uint8_t COMMON_NODE_HEADER_SIZE =
+    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;
+
+/*
+ * Leaf Node Header Layout
+ */
+const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE =
+    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+
+/*
+ * Leaf Node Body Layout
+ */
+const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_KEY_OFFSET = 0;
+const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
+const uint32_t LEAF_NODE_VALUE_OFFSET =
+    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
+const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
+const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
+const uint32_t LEAF_NODE_MAX_CELLS =
+    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
 
+void print_prompt();
 void print_row(Row *row);
+void print_constants();
+void print_leaf_node(void* node);
+
 InputBuffer *new_input_buffer();
-void print_prompt();
 void read_input(InputBuffer *input_buffer);
+
 MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table);
 PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
 PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
 ExecuteResult execute_insert(Statement *statement, Table *table);
 ExecuteResult execute_select(Statement *statement, Table *table);
 ExecuteResult execute_statement(Statement *statement, Table *table);
+
 Table *db_open(const char *filename);
 void db_close(Table *table);
 void serialize_row(Row *source, void *destination);
 void deserialize_row(void *source, Row *destination);
-void *row_slot(Table *table, uint32_t row_num);
 
 Pager *pager_open(const char *filename);
 void *get_page(Pager *pager, uint32_t page_num);
-void pager_flush(Pager* pager, uint32_t page_num, uint32_t size);
+void pager_flush(Pager* pager, uint32_t page_num);
 
 Cursor *table_start(Table *table);
 Cursor *table_end(Table *table);
+
 void cursor_advance(Cursor *cursor);
 void *cursor_value(Cursor *cursor);
 
+void initialize_leaf_node(void *node);
+uint32_t *leaf_node_num_cells(void *node);
+void *leaf_node_cell(void *node, uint32_t cell_num);
+uint32_t *leaf_node_key(void *node, uint32_t cell_num);
+void *leaf_node_value(void *node, uint32_t cell_num);
+void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
+
+
+void
+print_prompt()
+{
+    printf("db > ");
+}
+
 void
 print_row(Row *row)
 {
     printf("(%d, %s, %s)\n", row->id, row->username, row->email);
 }
 
+void
+print_constants()
+{
+    printf("ROW_SIZE: %lu\n", ROW_SIZE);
+    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
+    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
+    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
+    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
+    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
+}
+
+void
+print_leaf_node(void* node)
+{
+    uint32_t i;
+    uint32_t num_cells = *leaf_node_num_cells(node);
+
+    printf("leaf (size %d)\n", num_cells);
+    for (i = 0; i < num_cells; i++) {
+        uint32_t key = *leaf_node_key(node, i);
+        printf("  - %d : %d\n", i, key);
+    }
+}
+
 InputBuffer *
 new_input_buffer()
 {
@@ -143,12 +222,6 @@ new_input_buffer()
     return input_buffer;
 }
 
-void
-print_prompt()
-{
-    printf("db > ");
-}
-
 void
 read_input(InputBuffer *input_buffer)
 {
@@ -174,6 +247,14 @@ do_meta_command(InputBuffer *input_buffer, Table *table)
     if (strcmp(input_buffer->buffer, ".exit") == 0) {
         db_close(table);
         exit(EXIT_SUCCESS);
+    } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
+        printf("Tree:\n");
+        print_leaf_node(get_page(table->pager, 0));
+        return META_COMMAND_SUCCESS;
+    } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
+        printf("Constants:\n");
+        print_constants();
+        return META_COMMAND_SUCCESS;
     }
 
     return META_COMMAND_UNRECOGNIZED_COMMAND;
@@ -193,18 +274,55 @@ prepare_statement(InputBuffer *input_buffer, Statement *statement)
     return PREPARE_UNRECOGNIZED_STATEMENT;
 }
 
+PrepareResult
+prepare_insert(InputBuffer *input_buffer, Statement *statement)
+{
+    char *keyword = strtok(input_buffer->buffer, " ");
+    char *id_string = strtok(NULL, " ");
+    char *username = strtok(NULL, " ");
+    char *email = strtok(NULL, " ");
+
+    unused(keyword);
+
+    statement->type = STATEMENT_INSERT;
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
+
 ExecuteResult
 execute_insert(Statement *statement, Table *table)
 {
-    if (table->num_rows >= TABLE_MAX_ROWS) {
+    Row    *row_to_insert;
+    Cursor *cursor;
+    void   *node = get_page(table->pager, table->root_page_num);
+
+    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
         return EXECUTE_TABLE_FULL;
     }
 
-    Row *row_to_insert = &statement->row_to_insert;
-    Cursor *cursor = table_end(table);
+    row_to_insert = &statement->row_to_insert;
+    cursor = table_end(table);
 
-    serialize_row(row_to_insert, cursor_value(cursor));
-    table->num_rows += 1;
+    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
 
     free(cursor);
 
@@ -215,8 +333,10 @@ ExecuteResult
 execute_select(Statement *statement, Table *table)
 {
     Cursor *cursor = table_start(table);
+    Row     row;
+
+    unused(statement);
 
-    Row row;
     while (!(cursor->end_of_table)) {
         deserialize_row(cursor_value(cursor), &row);
         print_row(&row);
@@ -245,11 +365,16 @@ Table *
 db_open(const char *filename)
 {
     Pager    *pager = pager_open(filename);
-    uint32_t  num_rows = pager->file_length / ROW_SIZE;
     Table    *table = (Table *)malloc(sizeof(Table));
 
     table->pager = pager;
-    table->num_rows = num_rows;
+    table->root_page_num = 0;
+
+    if (pager->num_pages == 0) {
+        /* New database file. Initialize page 0 as leaf node. */
+        void *root_node = get_page(pager, 0);
+        initialize_leaf_node(root_node);
+    }
 
     return table;
 }
@@ -258,35 +383,19 @@ void
 db_close(Table *table)
 {
     Pager     *pager = table->pager;
-    uint32_t   num_full_pages = table->num_rows / ROWS_PER_PAGE;
-    uint32_t   num_additional_rows;
     uint32_t   i;
     int        result;
 
-    for (i = 0; i < num_full_pages; i++) {
+    for (i = 0; i < pager->num_pages; i++) {
         if (pager->pages[i] == NULL) {
             continue;
         }
 
-        pager_flush(pager, i, PAGE_SIZE);
+        pager_flush(pager, i);
         free(pager->pages[i]);
         pager->pages[i] = NULL;
     }
 
-    /*
-     * There may be a partial page to write to the end of the file.
-     * This should not be needed after we switch to a B-tree.
-     */
-    num_additional_rows = table->num_rows % ROWS_PER_PAGE;
-    if (num_additional_rows > 0) {
-        uint32_t page_num = num_full_pages;
-        if (pager->pages[page_num] != NULL) {
-            pager_flush(pager, page_num, num_additional_rows * ROW_SIZE);
-            free(pager->pages[page_num]);
-            pager->pages[page_num] = NULL;
-        }
-    }
-
     result = close(pager->file_descriptor);
     if (result == -1) {
         printf("Error closing db file.\n");
@@ -304,71 +413,6 @@ db_close(Table *table)
     free(table);
 }
 
-int
-main(int argc, char *argv[])
-{
-    char           *filename;
-    Table          *table;
-    InputBuffer    *input_buffer;
-
-    if (argc < 2) {
-        printf("Must supply a database filename.\n");
-        exit(EXIT_FAILURE);
-    }
-
-    filename = argv[1];
-    table = db_open(filename);
-    input_buffer = new_input_buffer();
-
-    while (true) {
-        print_prompt();
-        read_input(input_buffer);
-
-        if (input_buffer->buffer[0] == '.') {
-            switch (do_meta_command(input_buffer, table)) {
-            case META_COMMAND_SUCCESS:
-                continue;
-            case META_COMMAND_UNRECOGNIZED_COMMAND:
-                printf("Unrecognized command '%s'\n", input_buffer->buffer);
-                continue;
-            }
-        }
-
-        Statement statement;
-        switch (prepare_statement(input_buffer, &statement)) {
-        case PREPARE_SUCCESS:
-            break;
-        case PREPARE_NEGATIVE_ID:
-            printf("ID must be positive.\n");
-            continue;
-        case PREPARE_STRING_TOO_LONG:
-            printf("String is too long.\n");
-            continue;
-        case PREPARE_SYNTAX_ERROR:
-            printf("Syntax error. Could not parse statement.\n");
-            continue;
-        case PREPARE_UNRECOGNIZED_STATEMENT:
-            printf("Unrecognized keyword at start of '%s'.\n",
-                   input_buffer->buffer);
-            continue;
-        }
-
-        switch (execute_statement(&statement, table)) {
-        case EXECUTE_SUCCESS:
-            printf("Executed.\n");
-            break;
-        case EXECUTE_TABLE_FULL:
-            printf("Error: Table full.\n");
-            break;
-        case EXECUTE_UNKNOWN_STMT:
-            printf("Error: Unknown statement.\n");
-            break;
-        }
-    }
-
-    return 0;
-}
-
 void
 serialize_row(Row *source, void *destination)
 {
@@ -387,49 +431,6 @@ deserialize_row(void *source, Row *destination)
     memcpy(&(destination->email), src + EMAIL_OFFSET, EMAIL_SIZE);
 }
 
-void *
-cursor_value(Cursor *cursor)
-{
-    uint32_t   row_num = cursor->row_num;
-    uint32_t   page_num = row_num / ROWS_PER_PAGE;
-    void      *page = get_page(cursor->table->pager, page_num);
-    uint32_t    row_offset = row_num % ROWS_PER_PAGE;
-    uint32_t    byte_offset = row_offset * ROW_SIZE;
-    return (char *) page + byte_offset;
-}
-
-PrepareResult
-prepare_insert(InputBuffer *input_buffer, Statement *statement)
-{
-    statement->type = STATEMENT_INSERT;
-
-    char *keyword = strtok(input_buffer->buffer, " ");
-    char *id_string = strtok(NULL, " ");
-    char *username = strtok(NULL, " ");
-    char *email = strtok(NULL, " ");
-
-    if (id_string == NULL || username == NULL || email == NULL) {
-        return PREPARE_SYNTAX_ERROR;
-    }
-
-    int id = atoi(id_string);
-    if (id < 0) {
-        return PREPARE_NEGATIVE_ID;
-    }
-    if (strlen(username) > COLUMN_USERNAME_SIZE) {
-        return PREPARE_STRING_TOO_LONG;
-    }
-    if (strlen(email) > COLUMN_EMAIL_SIZE) {
-        return PREPARE_STRING_TOO_LONG;
-    }
-
-    statement->row_to_insert.id = id;
-    strcpy(statement->row_to_insert.username, username);
-    strcpy(statement->row_to_insert.email, email);
-
-    return PREPARE_SUCCESS;
-}
-
 Pager *
 pager_open(const char *filename)
 {
@@ -454,6 +455,12 @@ pager_open(const char *filename)
     pager = malloc(sizeof(Pager));
     pager->file_descriptor = fd;
     pager->file_length = file_length;
+    pager->num_pages = (file_length / PAGE_SIZE);
+
+    if (file_length % PAGE_SIZE != 0) {
+        printf("Db file is not a whole number of pages. Corrupt file.\n");
+        exit(EXIT_FAILURE);
+    }
 
     for (i = 0; i < TABLE_MAX_PAGES; i++) {
         pager->pages[i] = NULL;
@@ -492,13 +499,17 @@ get_page(Pager *pager, uint32_t page_num)
         }
 
         pager->pages[page_num] = page;
+
+        if (page_num >= pager->num_pages) {
+            pager->num_pages = page_num + 1;
+        }
     }
 
     return pager->pages[page_num];
 }
 
 void
-pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
+pager_flush(Pager* pager, uint32_t page_num)
 {
     off_t	offset;
     ssize_t  bytes_written;
@@ -515,7 +526,7 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
     }
 
     bytes_written =
-        write(pager->file_descriptor, pager->pages[page_num], size);
+        write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);
 
     if (bytes_written == -1) {
         printf("Error writing: %d\n", errno);
@@ -526,11 +537,17 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
 Cursor *
 table_start(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
 
     cursor->table = table;
-    cursor->row_num = 0;
-    cursor->end_of_table = (table->num_rows == 0);
+    cursor->page_num = table->root_page_num;
+    cursor->cell_num = 0;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->end_of_table = (num_cells == 0);
 
     return cursor;
 }
@@ -538,10 +555,16 @@ table_start(Table *table)
 Cursor *
 table_end(Table *table)
 {
-    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  num_cells;
+    void     *root_node;
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
 
     cursor->table = table;
-    cursor->row_num = table->num_rows;
+    cursor->page_num = table->root_page_num;
+
+    root_node = get_page(table->pager, table->root_page_num);
+    num_cells = *leaf_node_num_cells(root_node);
+    cursor->cell_num = num_cells;
     cursor->end_of_table = true;
 
     return cursor;
@@ -550,8 +573,141 @@ table_end(Table *table)
 void
 cursor_advance(Cursor *cursor)
 {
-    cursor->row_num += 1;
-    if (cursor->row_num >= cursor->table->num_rows) {
+    uint32_t  page_num = cursor->page_num;
+    void     *node = get_page(cursor->table->pager, page_num);
+
+    cursor->cell_num += 1;
+    if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
         cursor->end_of_table = true;
     }
 }
+
+void *
+cursor_value(Cursor *cursor)
+{
+    uint32_t   page_num = cursor->page_num;
+    void      *page = get_page(cursor->table->pager, page_num);
+
+    return leaf_node_value(page, cursor->cell_num);
+}
+
+void
+initialize_leaf_node(void *node)
+{
+    *leaf_node_num_cells(node) = 0;
+}
+
+uint32_t *
+leaf_node_num_cells(void *node)
+{
+    return node + LEAF_NODE_NUM_CELLS_OFFSET;
+}
+
+void *
+leaf_node_cell(void *node, uint32_t cell_num)
+{
+    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
+}
+
+uint32_t *
+leaf_node_key(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num);
+}
+
+void *
+leaf_node_value(void *node, uint32_t cell_num)
+{
+    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
+}
+
+void
+leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
+{
+    void *node = get_page(cursor->table->pager, cursor->page_num);
+
+    uint32_t num_cells = *leaf_node_num_cells(node);
+    if (num_cells >= LEAF_NODE_MAX_CELLS) {
+        /* Node full */
+        printf("Need to implement splitting a leaf node.\n");
+        exit(EXIT_FAILURE);
+    }
+
+    if (cursor->cell_num < num_cells) {
+        /* Make room for new cell */
+        uint32_t i;
+        for (i = num_cells; i > cursor->cell_num; i--) {
+            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1),
+                   LEAF_NODE_CELL_SIZE);
+        }
+    }
+
+    *(leaf_node_num_cells(node)) += 1;
+    *(leaf_node_key(node, cursor->cell_num)) = key;
+    serialize_row(value, leaf_node_value(node, cursor->cell_num));
+}
+
+int
+main(int argc, char *argv[])
+{
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
+
+    while (true) {
+        print_prompt();
+        read_input(input_buffer);
+
+        if (input_buffer->buffer[0] == '.') {
+            switch (do_meta_command(input_buffer, table)) {
+            case META_COMMAND_SUCCESS:
+                continue;
+            case META_COMMAND_UNRECOGNIZED_COMMAND:
+                printf("Unrecognized command '%s'\n", input_buffer->buffer);
+                continue;
+            }
+        }
+
+        Statement statement;
+        switch (prepare_statement(input_buffer, &statement)) {
+        case PREPARE_SUCCESS:
+            break;
+        case PREPARE_NEGATIVE_ID:
+            printf("ID must be positive.\n");
+            continue;
+        case PREPARE_STRING_TOO_LONG:
+            printf("String is too long.\n");
+            continue;
+        case PREPARE_SYNTAX_ERROR:
+            printf("Syntax error. Could not parse statement.\n");
+            continue;
+        case PREPARE_UNRECOGNIZED_STATEMENT:
+            printf("Unrecognized keyword at start of '%s'.\n",
+                   input_buffer->buffer);
+            continue;
+        }
+
+        switch (execute_statement(&statement, table)) {
+        case EXECUTE_SUCCESS:
+            printf("Executed.\n");
+            break;
+        case EXECUTE_TABLE_FULL:
+            printf("Error: Table full.\n");
+            break;
+        case EXECUTE_UNKNOWN_STMT:
+            printf("Error: Unknown statement.\n");
+            break;
+        }
+    }
+
+    return 0;
+}
```
