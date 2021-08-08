---
title: 09 - 二分查找及重复键
---

注意到在上篇文章中，我们仍在以未排序的顺序存储键。我们将该修复该问题，并检测和拒绝重复键。

现在，我们的 `execute_insert()` 函数总是选择在表的末尾插入。相反，我们应该在表中搜索要插入的正确位置，然后在那里插入。如果键已经存在，则返回一个错误。

```diff
@@ -314,13 +314,23 @@ execute_insert(Statement *statement, Table *table)
     Row    *row_to_insert;
     Cursor *cursor;
     void   *node = get_page(table->pager, table->root_page_num);
+    uint32_t num_cells = (*leaf_node_num_cells(node));
+    uint32_t key_to_insert;

-    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
+    if (num_cells >= LEAF_NODE_MAX_CELLS) {
         return EXECUTE_TABLE_FULL;
     }

     row_to_insert = &statement->row_to_insert;
-    cursor = table_end(table);
+    key_to_insert = row_to_insert->id;
+    cursor = table_find(table, key_to_insert);
+
+    if (cursor->cell_num < num_cells) {
+        uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
+        if (key_at_index == key_to_insert) {
+            return EXECUTE_DUPLICATE_KEY;
+        }
+    }

     leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
```

我们不再需要 `table_end()` 函数了。

```diff
-Cursor *
-table_end(Table *table)
-{
-    uint32_t  num_cells;
-    void     *root_node;
-    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
-
-    cursor->table = table;
-    cursor->page_num = table->root_page_num;
-
-    root_node = get_page(table->pager, table->root_page_num);
-    num_cells = *leaf_node_num_cells(root_node);
-    cursor->cell_num = num_cells;
-    cursor->end_of_table = true;
-
-    return cursor;
-}
```
我们将通过一个方法来替代它，该方法在树中查找给定的键。

```diff
+/*
+ * Return the position of the given key.
+ * If the key is not present, return the position
+ * where it should be inserted.
+ */
+Cursor *
+table_find(Table *table, uint32_t key)
+{
+    uint32_t  root_page_num = table->root_page_num;
+    void     *root_node = get_page(table->pager, root_page_num);
+
+    if (get_node_type(root_node) == NODE_LEAF) {
+        return leaf_node_find(table, root_page_num, key);
+    } else {
+        printf("Need to implement searching an internal node\n");
+        exit(EXIT_FAILURE);
+    }
+}
```

我正在为内部节点删除分支，因为我们还没有实现内部节点。我们可以使用二进制搜索来搜索叶节点。

```diff
+Cursor *
+leaf_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    uint32_t  min_index = 0;
+    uint32_t  one_past_max_index;
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  num_cells = *leaf_node_num_cells(node);
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->page_num = page_num;
+
+    /* Binary search */
+    one_past_max_index = num_cells;
+
+    while (one_past_max_index != min_index) {
+        uint32_t index = (min_index + one_past_max_index) / 2;
+        uint32_t key_at_index = *leaf_node_key(node, index);
+        if (key == key_at_index) {
+            cursor->cell_num = index;
+            return cursor;
+        }
+
+        if (key < key_at_index) {
+            one_past_max_index = index;
+        } else {
+            min_index = index + 1;
+        }
+    }
+
+    cursor->cell_num = min_index;
+    return cursor;
+}
```
这将返回

- 键所在的位置，
- 如果我们想插入新键，我们需要移动的另一个键的位置，或者
- 最后一个键后的位置

因为我们现在检查节点类型，所以需要函数来获取和设置节点的类型。

```diff
+NodeType
+get_node_type(void *node)
+{
+    uint8_t value = *((uint8_t *)(node + NODE_TYPE_OFFSET));
+    return (NodeType) value;
+}
+
+void
+set_node_type(void *node, NodeType type)
+{
+    uint8_t value = type;
+    *((uint8_t *)(node + NODE_TYPE_OFFSET)) = value;
+}
```

我们必须首先转换为 `uint8_t` 以确保它被序列化为单个字节。

我们还需要初始化节点类型。

```diff
 void
 initialize_leaf_node(void *node)
 {
+    set_node_type(node, NODE_LEAF);
     *leaf_node_num_cells(node) = 0;
 }
```

最后，我们需要制作和处理一个新的错误代码。

```diff
 enum ExecuteResult_t
 {
     EXECUTE_SUCCESS,
+    EXECUTE_DUPLICATE_KEY,
     EXECUTE_TABLE_FULL,
     EXECUTE_UNKNOWN_STMT
 };
```

```diff
         case EXECUTE_SUCCESS:
             printf("Executed.\n");
             break;
+        case EXECUTE_DUPLICATE_KEY:
+            printf("Error: Duplicate key.\n");
+            break;
         case EXECUTE_TABLE_FULL:
             printf("Error: Table full.\n");
             break;
```

通过这些更改，我们的测试可以更改以检查排序顺序：

```diff
       "db > Executed.",
       "db > Tree:",
       "leaf (size 3)",
-      "  - 0 : 3",
-      "  - 1 : 1",
-      "  - 2 : 2",
+      "  - 0 : 1",
+      "  - 1 : 2",
+      "  - 2 : 3",
       "db > "
     ])
   end
```

我们可以为重复键添加一个新测试：

```diff
+  it 'prints an error message if there is a duplicate id' do
+    script = [
+      "insert 1 user1 person1@example.com",
+      "insert 1 user1 person1@example.com",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Error: Duplicate key.",
+      "db > (1, user1, person1@example.com)",
+      "Executed.",
+      "db > ",
+    ])
+  end
```

仅此而已！接下来：我们将实现分裂叶节点并创建内部节点。

下面是译者自己加的。

## 完整的 diff

```
diff --git a/db.c b/db.c
index f85211b..db973cf 100644
--- a/db.c
+++ b/db.c
@@ -21,6 +21,7 @@ typedef struct InputBuffer_t InputBuffer;
 enum ExecuteResult_t
 {
     EXECUTE_SUCCESS,
+    EXECUTE_DUPLICATE_KEY,
     EXECUTE_TABLE_FULL,
     EXECUTE_UNKNOWN_STMT
 };
@@ -162,7 +163,7 @@ void *get_page(Pager *pager, uint32_t page_num);
 void pager_flush(Pager* pager, uint32_t page_num);
 
 Cursor *table_start(Table *table);
-Cursor *table_end(Table *table);
+Cursor *table_find(Table *table, uint32_t key);
 
 void cursor_advance(Cursor *cursor);
 void *cursor_value(Cursor *cursor);
@@ -173,7 +174,10 @@ void *leaf_node_cell(void *node, uint32_t cell_num);
 uint32_t *leaf_node_key(void *node, uint32_t cell_num);
 void *leaf_node_value(void *node, uint32_t cell_num);
 void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
+Cursor *leaf_node_find(Table *table, uint32_t page_num, uint32_t key);
 
+NodeType get_node_type(void *node);
+void set_node_type(void *node, NodeType type);
 
 void
 print_prompt()
@@ -314,13 +318,23 @@ execute_insert(Statement *statement, Table *table)
     Row    *row_to_insert;
     Cursor *cursor;
     void   *node = get_page(table->pager, table->root_page_num);
+    uint32_t num_cells = (*leaf_node_num_cells(node));
+    uint32_t key_to_insert;
 
-    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELLS)) {
+    if (num_cells >= LEAF_NODE_MAX_CELLS) {
         return EXECUTE_TABLE_FULL;
     }
 
     row_to_insert = &statement->row_to_insert;
-    cursor = table_end(table);
+    key_to_insert = row_to_insert->id;
+    cursor = table_find(table, key_to_insert);
+
+    if (cursor->cell_num < num_cells) {
+        uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
+        if (key_at_index == key_to_insert) {
+            return EXECUTE_DUPLICATE_KEY;
+        }
+    }
 
     leaf_node_insert(cursor, row_to_insert->id, row_to_insert);
 
@@ -552,22 +566,23 @@ table_start(Table *table)
     return cursor;
 }
 
+/*
+ * Return the position of the given key.
+ * If the key is not present, return the position
+ * where it should be inserted.
+ */
 Cursor *
-table_end(Table *table)
+table_find(Table *table, uint32_t key)
 {
-    uint32_t  num_cells;
-    void     *root_node;
-    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
+    uint32_t  root_page_num = table->root_page_num;
+    void     *root_node = get_page(table->pager, root_page_num);
 
-    cursor->table = table;
-    cursor->page_num = table->root_page_num;
-
-    root_node = get_page(table->pager, table->root_page_num);
-    num_cells = *leaf_node_num_cells(root_node);
-    cursor->cell_num = num_cells;
-    cursor->end_of_table = true;
-
-    return cursor;
+    if (get_node_type(root_node) == NODE_LEAF) {
+        return leaf_node_find(table, root_page_num, key);
+    } else {
+        printf("Need to implement searching an internal node\n");
+        exit(EXIT_FAILURE);
+    }
 }
 
 void
@@ -594,6 +609,7 @@ cursor_value(Cursor *cursor)
 void
 initialize_leaf_node(void *node)
 {
+    set_node_type(node, NODE_LEAF);
     *leaf_node_num_cells(node) = 0;
 }
 
@@ -647,6 +663,54 @@ leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
     serialize_row(value, leaf_node_value(node, cursor->cell_num));
 }
 
+Cursor *
+leaf_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    uint32_t  min_index = 0;
+    uint32_t  one_past_max_index;
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  num_cells = *leaf_node_num_cells(node);
+    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
+
+    cursor->table = table;
+    cursor->page_num = page_num;
+
+    /* Binary search */
+    one_past_max_index = num_cells;
+
+    while (one_past_max_index != min_index) {
+        uint32_t index = (min_index + one_past_max_index) / 2;
+        uint32_t key_at_index = *leaf_node_key(node, index);
+        if (key == key_at_index) {
+            cursor->cell_num = index;
+            return cursor;
+        }
+
+        if (key < key_at_index) {
+            one_past_max_index = index;
+        } else {
+            min_index = index + 1;
+        }
+    }
+
+    cursor->cell_num = min_index;
+    return cursor;
+}
+
+NodeType
+get_node_type(void *node)
+{
+    uint8_t value = *((uint8_t *)(node + NODE_TYPE_OFFSET));
+    return (NodeType) value;
+}
+
+void
+set_node_type(void *node, NodeType type)
+{
+    uint8_t value = type;
+    *((uint8_t *)(node + NODE_TYPE_OFFSET)) = value;
+}
+
 int
 main(int argc, char *argv[])
 {
@@ -700,6 +764,9 @@ main(int argc, char *argv[])
         case EXECUTE_SUCCESS:
             printf("Executed.\n");
             break;
+        case EXECUTE_DUPLICATE_KEY:
+            printf("Error: Duplicate key.\n");
+            break;
         case EXECUTE_TABLE_FULL:
             printf("Error: Table full.\n");
             break;
```

测试用例。

```diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index 652f9fa..e649f2d 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -142,11 +142,28 @@ describe 'database' do
       "db > Executed.",
       "db > Tree:",
       "leaf (size 3)",
-      "  - 0 : 3",
-      "  - 1 : 1",
-      "  - 2 : 2",
+      "  - 0 : 1",
+      "  - 1 : 2",
+      "  - 2 : 3",
       "db > "
     ])
   end
 
+  it 'prints an error message if there is a duplicate id' do
+    script = [
+      "insert 1 user1 person1@example.com",
+      "insert 1 user1 person1@example.com",
+      "select",
+      ".exit",
+    ]
+    result = run_script(script)
+    expect(result).to match_array([
+      "db > Executed.",
+      "db > Error: Duplicate key.",
+      "db > (1, user1, person1@example.com)",
+      "Executed.",
+      "db > ",
+    ])
+  end
+
 end
```
