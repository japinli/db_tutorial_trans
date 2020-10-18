---
title: 06 - 数据库游标
date: 2020-10-18
---

这部分内容比上一部分内容要短小一些。我们将对其进行一些细小的重构以便下一节的 B-Tree 实现更为容易。

我们将在这部分内容中引入游标对象，游标对象表示表中的位置。您可以通过游标做以下的事情：

- 在表的开始处创建游标
- 在表的末尾处创建游标
- 访问游标所指向的行数据
- 移动游标到下一行

这些行为都是我们现在需要实现的。后续我们还需要实现：

- 删除游标所指向的行
- 修改游标所指向的行
- 根据 ID 查询表，并创建一个游标执行该行

闲话少说，让我们来看看游标 `Cursor` 类型的定义：

```diff
+typedef struct {
+    Table    *table;
+    uint32_t  row_num;
+    bool      end_of_table; /* Indicates a position one past the last element */
+} Cursor;
```

对于我们现在的表数据结果而言，您所需要的仅仅是一个行编号就可以定义表中的数据。

一个游标也有一个对它所属的表的引用（所以我们的游标函数可以只把游标作为参数）。


最后，它有一个布尔值 `end_of_table`。这样我们就可以在表的末尾表示一个位置（也就是我们可能想要插入一行的地方）。

函数 `table_start()` 和 `table_end()` 用于创建新的游标：

``` diff
+Cursor *
+table_start(Table *table)
+{
+    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->row_num = 0;
+    cursor->end_of_table = (table->num_rows == 0);
+
+    return cursor;
+}
+
+Cursor *
+table_end(Table *table)
+{
+    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->row_num = table->num_rows;
+    cursor->end_of_table = true;
+
+    return cursor;
+}
```

我们的 `row_slot()` 函数将变成 `cursor_value()`，它返回一个有游标描述位置的指针：

```diff
void *
-row_slot(Table *table, uint32_t row_num)
+cursor_value(Cursor *cursor)
 {
+    uint32_t   row_num = cursor->row_num;
     uint32_t   page_num = row_num / ROWS_PER_PAGE;
-    void      *page = get_page(table->pager, page_num);
+    void      *page = get_page(cursor->table->pager, page_num);
     uint32_t    row_offset = row_num % ROWS_PER_PAGE;
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
```

在我们当前的表结构中推进光标就像递增行号一样简单。在 B-Tree 中，这将会更复杂一些。

``` diff
+void
+cursor_advance(Cursor *cursor)
+{
+    cursor->row_num += 1;
+    if (cursor->row_num >= cursor->table->num_rows) {
+        cursor->end_of_table = true;
+    }
+}
```

最后，我们使用抽象的游标来重写后端虚拟机的代码。当插入一行数据时，我们在表的末尾创建一个游标，写入数据并关闭游标。

```diff
     Row *row_to_insert = &statement->row_to_insert;
-    serialize_row(row_to_insert, row_slot(table, table->num_rows));
+    Cursor *cursor = table_end(table);
+
+    serialize_row(row_to_insert, cursor_value(cursor));
     table->num_rows += 1;

+    free(cursor);
+
     return EXECUTE_SUCCESS;
 }
```

当查询表中的所有数据时，我们在表的开始处创建游标，打印行记录并移动到下一行。重复这个过程直到到达表末尾。

``` diff
 ExecuteResult
 execute_select(Statement *statement, Table *table)
 {
+    Cursor *cursor = table_start(table);
+
     Row row;
-    for (uint32_t i = 0; i < table->num_rows; i++) {
-        deserialize_row(row_slot(table, i), &row);
+    while (!(cursor->end_of_table)) {
+        deserialize_row(cursor(cursor), &row);
         print_row(&row);
+        cursor_advance(cursor);
 }

+    free(cursor);
+
     return EXECUTE_SUCCESS;
 }
```

好了，就这样吧！我说过，这是一个较短的重构，应该能帮助我们重写。就像我说的，这是一个较短的重构，当我们把表的数据结构重写成 B-Tree 时，它应该能帮助我们。

函数 `execute_select()` 和 `execute_insert()` 可以完全通过游标与表交互，而不需要假设表是如何存储的。

下面是完整的 diff 文件:

``` diff
diff --git a/db.c b/db.c
index c13021c..14ab8d1 100644
--- a/db.c
+++ b/db.c
@@ -94,6 +94,12 @@ struct Table_t
 };
 typedef struct Table_t Table;

+typedef struct {
+    Table    *table;
+    uint32_t  row_num;
+    bool      end_of_table; /* Indicates a position one past the last element */
+} Cursor;
+

 void print_row(Row *row);
 InputBuffer *new_input_buffer();
@@ -115,6 +121,11 @@ Pager *pager_open(const char *filename);
 void *get_page(Pager *pager, uint32_t page_num);
 void pager_flush(Pager* pager, uint32_t page_num, uint32_t size);

+Cursor *table_start(Table *table);
+Cursor *table_end(Table *table);
+void cursor_advance(Cursor *cursor);
+void *cursor_value(Cursor *cursor);
+
 void
 print_row(Row *row)
 {
@@ -190,21 +201,30 @@ execute_insert(Statement *statement, Table *table)
     }

     Row *row_to_insert = &statement->row_to_insert;
-    serialize_row(row_to_insert, row_slot(table, table->num_rows));
+    Cursor *cursor = table_end(table);
+
+    serialize_row(row_to_insert, cursor_value(cursor));
     table->num_rows += 1;

+    free(cursor);
+
     return EXECUTE_SUCCESS;
 }

 ExecuteResult
 execute_select(Statement *statement, Table *table)
 {
+    Cursor *cursor = table_start(table);
+
     Row row;
-    for (uint32_t i = 0; i < table->num_rows; i++) {
-        deserialize_row(row_slot(table, i), &row);
+    while (!(cursor->end_of_table)) {
+        deserialize_row(cursor(cursor), &row);
         print_row(&row);
+        cursor_advance(cursor);
     }

+    free(cursor);
+
     return EXECUTE_SUCCESS;
 }

@@ -368,10 +388,11 @@ deserialize_row(void *source, Row *destination)
 }

 void *
-row_slot(Table *table, uint32_t row_num)
+cursor_value(Cursor *cursor)
 {
+    uint32_t   row_num = cursor->row_num;
     uint32_t   page_num = row_num / ROWS_PER_PAGE;
-    void      *page = get_page(table->pager, page_num);
+    void      *page = get_page(cursor->table->pager, page_num);
     uint32_t    row_offset = row_num % ROWS_PER_PAGE;
     uint32_t    byte_offset = row_offset * ROW_SIZE;
     return (char *) page + byte_offset;
@@ -501,3 +522,36 @@ pager_flush(Pager* pager, uint32_t page_num, uint32_t size)
         exit(EXIT_FAILURE);
     }
 }
+
+Cursor *
+table_start(Table *table)
+{
+    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->row_num = 0;
+    cursor->end_of_table = (table->num_rows == 0);
+
+    return cursor;
+}
+
+Cursor *
+table_end(Table *table)
+{
+    Cursor *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->row_num = table->num_rows;
+    cursor->end_of_table = true;
+
+    return cursor;
+}
+
+void
+cursor_advance(Cursor *cursor)
+{
+    cursor->row_num += 1;
+    if (cursor->row_num >= cursor->table->num_rows) {
+        cursor->end_of_table = true;
+    }
+}
```
