---
title: 12 - 多级 B-Tree 扫描
---

我们现在支持构建一个多级 btree，但是在这个过程中我们破坏了 `select` 语句。下面是一个测试案例，它插入了 15 行，然后试图打印它们。

```diff
+  it 'prints all rows in a multi-level tree' do
+    script = []
+    (1..15).each do |i|
+      script << "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << "select"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result[15...result.length]).to match_array([
+      "db > (1, user1, person1@example.com)",
+      "(2, user2, person2@example.com)",
+      "(3, user3, person3@example.com)",
+      "(4, user4, person4@example.com)",
+      "(5, user5, person5@example.com)",
+      "(6, user6, person6@example.com)",
+      "(7, user7, person7@example.com)",
+      "(8, user8, person8@example.com)",
+      "(9, user9, person9@example.com)",
+      "(10, user10, person10@example.com)",
+      "(11, user11, person11@example.com)",
+      "(12, user12, person12@example.com)",
+      "(13, user13, person13@example.com)",
+      "(14, user14, person14@example.com)",
+      "(15, user15, person15@example.com)",
+      "Executed.", "db > ",
+    ])
+  end
```

但当我们现在运行这个测试案例时，实际发生的情况是：

```
db > select
(2, user1, person1@example.com)
Executed.
```

这很奇怪。它只打印了一行，而且那一行看起来已经损坏了（注意到 ID 与用户名不匹配）。

这种奇怪的现象是因为 `execute_select()` 函数从表的开始读取数据，而我们目前实现的 `table_start()` 函数返回根节点的 0 号 `cell`。但是我们的树的根节点现在作为一个内部节点，它并不包含任何行数据。输出的数据一定是根节点作为叶子节点时留下的。`execute_select()` 函数应该真正返回最左边叶子节点的 0 号 `cell`。

因此，我们需要删除原有的实现：

```diff
-Cursor *
-table_start(Table *table)
-{
-    uint32_t  num_cells;
-    void     *root_node;
-    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
-
-    cursor->table = table;
-    cursor->page_num = table->root_page_num;
-    cursor->cell_num = 0;
-
-    root_node = get_page(table->pager, table->root_page_num);
-    num_cells = *leaf_node_num_cells(root_node);
-    cursor->end_of_table = (num_cells == 0);
-
-    return cursor;
-}
```

接着添加一个新的实现，搜索键 0（可能的最小键）。即使键 0 在表中不存在，这个方法也会返回最低 `id` 的位置（从最左边的叶子节点的开始）。

```diff
+Cursor *
+table_start(Table *table)
+{
+    Cursor   *cursor = table_find(table, 0);
+    void     *node = get_page(table->pager, cursor->page_num);
+    uint32_t  num_cells = *leaf_node_num_cells(node);
+
+    cursor->end_of_table = (num_cells == 0);
+
+    return cursor;
+}
```

通过上述修改，它仍然只打印出一个叶子节点的行记录：

```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
Executed.
db >
```

在包含有 15 个记录的表中，我们的 btree 由一个内部节点和两个叶子节点组成，看起来像这样：

{% include image.html url="assets/images/btree3.png" description="我们的 btree 结构" %}

要扫描整个表，我们需要在到达第一个叶子节点的末尾后跳转到第二个叶子节点。为此，我们将在叶子节点头中保存一个名为 `next_leaf` 的新字段，该字段将保存右侧兄弟叶子节点的页编号。最右边的叶子节点的 `next_leaf` 为 0，表示没有兄弟节点（第 0 页仍保留给表的根节点）。

我们需要更新叶子节点标头格式以包含新字段：

```diff
 const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
 const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
-const uint32_t LEAF_NODE_HEADER_SIZE =
-    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
+    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
+    LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;
```

随后，添加访问新字段的方法：

```diff
+uint32_t *
+leaf_node_next_leaf(void *node)
+{
+    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
+}
```

初始化新叶子节点时，默认将 `next_leaf` 设置为 0：

```diff
     set_node_type(node, NODE_LEAF);
     set_node_root(node, false);
     *leaf_node_num_cells(node) = 0;
+    *leaf_node_next_leaf(node) = 0; // 0 represents no sibling
 }
```

每当我们分裂一个叶子节点时，更新兄弟指针。旧叶子节点的兄弟变成新叶子节点的兄弟，新叶子节点变成旧叶子节点的兄弟。

```diff
@@ -771,6 +788,9 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)

     initialize_leaf_node(new_node);

+    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
+    *leaf_node_next_leaf(old_node) = new_page_num;
```

新增的字段会更改一些常量，因此需要修改测试用例：

```diff
       "db > Constants:",
       "ROW_SIZE: 293",
       "COMMON_NODE_HEADER_SIZE: 6",
-      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_HEADER_SIZE: 14",
       "LEAF_NODE_CELL_SIZE: 297",
-      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4082",
       "LEAF_NODE_MAX_CELLS: 13",
       "db > ",
     ])
```

现在，每当我们想将光标移过叶子节点的末端时，我们都可以检查叶子节点是否有同级的兄弟节点。如果有，就跳过去。否则，我们就到了表的末尾。

```diff
@@ -642,7 +658,16 @@ cursor_advance(Cursor *cursor)

     cursor->cell_num += 1;
     if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
-        cursor->end_of_table = true;
+        /* Advance to next leaf node. */
+        uint32_t next_page_num = *leaf_node_next_leaf(node);
+        if (next_page_num == 0) {
+            /* This was rightmost leaf. */
+            cursor->end_of_table = true;
+        } else {
+            cursor->page_num = next_page_num;
+            cursor->cell_num = 0;
+        }
     }
 }
```

在这些更改之后，我们实际上能够输出了 15 行记录...

```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
(8, user8, person8@example.com)
(9, user9, person9@example.com)
(10, user10, person10@example.com)
(11, user11, person11@example.com)
(12, user12, person12@example.com)
(13, user13, person13@example.com)
(1919251317, 14, on14@example.com)
(15, user15, person15@example.com)
Executed.
db >
```

...但其中一个看起来已经损坏。

```
(1919251317, 14, on14@example.com)
```

经过调试，我发现这是因为我们分裂叶子节点的方式存在错误：

```diff
@@ -791,7 +818,9 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
         destination = leaf_node_cell(destination_node, index_within_node);

         if ((uint32_t) i == cursor->cell_num) {
-            serialize_row(value, destination);
+            serialize_row(value,
+                          leaf_node_value(destination_node, index_within_node));
+            *leaf_node_key(destination_node, index_within_node) = key;
```

请记住，叶子节点中的每个 `cell` 首先由一个键组成，然后由一个值组成：

{% include image.html url="assets/images/leaf-node-format.png" description="原始叶子节点格式" %}

我们将新行（值）写入到 `cell` 的开头，也就是键的位置。这意味着部分用户名被写进了 `id` 部分（因此导致异常的 `id`）。

修复该错误后，我们最终按预期打输出整个表：

```
db > select
(1, user1, person1@example.com)
(2, user2, person2@example.com)
(3, user3, person3@example.com)
(4, user4, person4@example.com)
(5, user5, person5@example.com)
(6, user6, person6@example.com)
(7, user7, person7@example.com)
(8, user8, person8@example.com)
(9, user9, person9@example.com)
(10, user10, person10@example.com)
(11, user11, person11@example.com)
(12, user12, person12@example.com)
(13, user13, person13@example.com)
(14, user14, person14@example.com)
(15, user15, person15@example.com)
Executed.
db >
```

哇！一个接一个的错误，但我们正在取得进展。

直到下一次。

## 完整的 diff

```diff

diff --git a/db.c b/db.c
index e3fb7b3..c89688a 100644
--- a/db.c
+++ b/db.c
@@ -122,8 +122,11 @@ const uint8_t COMMON_NODE_HEADER_SIZE =
  */
 const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
 const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
-const uint32_t LEAF_NODE_HEADER_SIZE =
-    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
+const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
+    LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE;
+const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
+    LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;
 
 /*
  * Leaf Node Body Layout
@@ -201,6 +204,7 @@ void *leaf_node_value(void *node, uint32_t cell_num);
 void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);
 Cursor *leaf_node_find(Table *table, uint32_t page_num, uint32_t key);
 void leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value);
+uint32_t *leaf_node_next_leaf(void *node);
 
 NodeType get_node_type(void *node);
 void set_node_type(void *node, NodeType type);
@@ -619,16 +623,10 @@ pager_flush(Pager* pager, uint32_t page_num)
 Cursor *
 table_start(Table *table)
 {
-    uint32_t  num_cells;
-    void     *root_node;
-    Cursor   *cursor = (Cursor *) malloc(sizeof(Cursor));
-
-    cursor->table = table;
-    cursor->page_num = table->root_page_num;
-    cursor->cell_num = 0;
+    Cursor   *cursor = table_find(table, 0);
+    void     *node = get_page(table->pager, cursor->page_num);
+    uint32_t  num_cells = *leaf_node_num_cells(node);
 
-    root_node = get_page(table->pager, table->root_page_num);
-    num_cells = *leaf_node_num_cells(root_node);
     cursor->end_of_table = (num_cells == 0);
 
     return cursor;
@@ -660,7 +658,15 @@ cursor_advance(Cursor *cursor)
 
     cursor->cell_num += 1;
     if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
-        cursor->end_of_table = true;
+        /* Advance to next leaf node. */
+        uint32_t next_page_num = *leaf_node_next_leaf(node);
+        if (next_page_num == 0) {
+            /* This was rightmost leaf. */
+            cursor->end_of_table = true;
+        } else {
+            cursor->page_num = next_page_num;
+            cursor->cell_num = 0;
+        }
     }
 }
 
@@ -679,6 +685,7 @@ initialize_leaf_node(void *node)
     set_node_type(node, NODE_LEAF);
     set_node_root(node, false);
     *leaf_node_num_cells(node) = 0;
+    *leaf_node_next_leaf(node) = 0; // 0 represents no sibling
 }
 
 void
@@ -781,7 +788,6 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
      * Insert the new value in one of the two nodes.
      * Update parent or create a new parent.
      */
-
     int32_t   i;
     void     *old_node = get_page(cursor->table->pager, cursor->page_num);
     uint32_t  new_page_num = get_unused_page_num(cursor->table->pager);
@@ -789,6 +795,9 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
 
     initialize_leaf_node(new_node);
 
+    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
+    *leaf_node_next_leaf(old_node) = new_page_num;
+
     /*
      * All existing keys plus new key should be divided
      * evenly between old (left) and new (right) nodes.
@@ -809,7 +818,9 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
         destination = leaf_node_cell(destination_node, index_within_node);
 
         if ((uint32_t) i == cursor->cell_num) {
-            serialize_row(value, destination);
+            serialize_row(value,
+                          leaf_node_value(destination_node, index_within_node));
+            *leaf_node_key(destination_node, index_within_node) = key;
         } else if ((uint32_t) i > cursor->cell_num) {
             memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE);
         } else {
@@ -829,6 +840,12 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
     }
 }
 
+uint32_t *
+leaf_node_next_leaf(void *node)
+{
+    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
+}
+
 NodeType
 get_node_type(void *node)
 {
```

测试用例。

```diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index 5ed35b0..f488df6 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -127,9 +127,9 @@ describe 'database' do
       "db > Constants:",
       "ROW_SIZE: 293",
       "COMMON_NODE_HEADER_SIZE: 6",
-      "LEAF_NODE_HEADER_SIZE: 10",
+      "LEAF_NODE_HEADER_SIZE: 14",
       "LEAF_NODE_CELL_SIZE: 297",
-      "LEAF_NODE_SPACE_FOR_CELLS: 4086",
+      "LEAF_NODE_SPACE_FOR_CELLS: 4082",
       "LEAF_NODE_MAX_CELLS: 13",
       "db > ",
     ])
@@ -207,4 +207,33 @@ describe 'database' do
     ])
   end
 
+  it 'prints all rows in a multi-level tree' do
+    script = []
+    (1..15).each do |i|
+      script << "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << "select"
+    script << ".exit"
+    result = run_script(script)
+
+    expect(result[15...result.length]).to match_array([
+      "db > (1, user1, person1@example.com)",
+      "(2, user2, person2@example.com)",
+      "(3, user3, person3@example.com)",
+      "(4, user4, person4@example.com)",
+      "(5, user5, person5@example.com)",
+      "(6, user6, person6@example.com)",
+      "(7, user7, person7@example.com)",
+      "(8, user8, person8@example.com)",
+      "(9, user9, person9@example.com)",
+      "(10, user10, person10@example.com)",
+      "(11, user11, person11@example.com)",
+      "(12, user12, person12@example.com)",
+      "(13, user13, person13@example.com)",
+      "(14, user14, person14@example.com)",
+      "(15, user15, person15@example.com)",
+      "Executed.", "db > ",
+    ])
+  end
+
 end
```
