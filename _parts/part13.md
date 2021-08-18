---
title: 13 - 节点分裂的数据更新
---

我们的 b-tree 实现之旅的下一步是处理分裂叶子节点之后对父节点进行修复。我们将使用下面的例子作为参考：

{% include image.html url="assets/images/updating-internal-node.png" description="更新内部节点的示例" %}

在这个例子中，我们添加了键 `3` 到树中。这将导致左叶子节点分裂。叶子节点分裂之后，我们通过以下步骤来修复树：

1. 将父节点中的第一个键更新为左子节点中最大的键（`3`）
2. 在更新键之后添加一个新的指针/键对
  - 新的指针指向新的子节点
  - 新的键设置为新节点中最大的键值（`5`）

所以首先，用两个新的函数调用替换我们的现有代码：`update_internal_node_key()` 用于步骤 `1`，`internal_node_insert()` 用于步骤 `2`。

```diff
@@ -790,11 +790,13 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
      */
     int32_t   i;
     void     *old_node = get_page(cursor->table->pager, cursor->page_num);
+    uint32_t  old_max = get_node_max_key(old_node);
     uint32_t  new_page_num = get_unused_page_num(cursor->table->pager);
     void     *new_node = get_page(cursor->table->pager, new_page_num);

     initialize_leaf_node(new_node);

+    *node_parent(new_node) = *node_parent(old_node);
     *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
     *leaf_node_next_leaf(old_node) = new_page_num;

@@ -835,8 +837,12 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
     if (is_node_root(old_node)) {
         return create_new_root(cursor->table, new_page_num);
     } else {
-        printf("Need to implement updating parent after split\n");
-        exit(EXIT_FAILURE);
+        uint32_t   parent_page_num = *node_parent(old_node);
+        uint32_t   new_max = get_node_max_key(old_node);
+        void      *parent = get_page(cursor->table->pager, parent_page_num);
+
+        update_internal_node_key(parent, old_max, new_max);
+        internal_node_insert(cursor->table, parent_page_num, new_page_num);
     }
 }
```

为了获得对父节点的引用，我们需要在页面的头部记录一个父节点的指针。

```diff
+uint32_t *
+node_parent(void *node)
+{
+    return node + PARENT_POINTER_OFFSET;
+}
```
```diff
@@ -923,6 +930,8 @@ create_new_root(Table *table, uint32_t right_child_page_num)
     left_child_max_key = get_node_max_key(left_child);
     *internal_node_key(root, 0) = left_child_max_key;
     *internal_node_right_child(root) = right_child_page_num;
+    *node_parent(left_child) = table->root_page_num;
+    *node_parent(right_child) = table->root_page_num;
 }
```

现在我们需要在父节点中找到受影响的 `cell`。孩子节点不知道自己的页编号，所以我们无法查找。但它确实知道自己的最大键，因此我们可以在父项中搜索该键。

```diff
+void
+update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key)
+{
+    uint32_t old_child_index = internal_node_find_child(node, old_key);
+    *internal_node_key(node, old_child_index) = new_key;
+}
```

在 `internal_node_find_child()` 函数中，我们将重用一些我们已有的代码来查找内部节点中的键。重构 `internal_node_find()` 以使用新的辅助方法。

```diff
-Cursor *
-internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+uint32_t
+internal_node_find_child(void *node, uint32_t key)
 {
-    void     *node = get_page(table->pager, page_num);
-    uint32_t  num_keys = *internal_node_num_keys(node);
-    uint32_t  child_num;
-    void     *child;
+    /*
+     * Return the index of the child which should contain
+     * the given key.
+     */
+    uint32_t num_keys = *internal_node_num_keys(node);

-    /* Binary search to find index of child to search. */
-    uint32_t  min_index = 0;
-    uint32_t  max_index = num_keys; /* There is one more child than key. */
+    /* Binary search. */
+    uint32_t min_index = 0;
+    uint32_t max_index = num_keys; /* there is one more child than key */

     while (min_index != max_index) {
         uint32_t index = (min_index + max_index) / 2;
@@ -985,8 +1001,16 @@ internal_node_find(Table *table, uint32_t page_num, uint32_t key)
         }
     }

-    child_num = *internal_node_child(node, min_index);
-    child = get_page(table->pager, child_num);
+    return min_index;
+}
+
+Cursor *
+internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  child_index = internal_node_find_child(node, key);
+    uint32_t  child_num = *internal_node_child(node, child_index);
+    void     *child = get_page(table->pager, child_num);
     switch (get_node_type(child)) {
     case NODE_LEAF:
         return leaf_node_find(table, child_num, key);
```

现在我们进入本文的核心，实现 `internal_node_insert()`。我会分步骤解释。

```diff
+void
+internal_node_insert(Table *table, uint32_t parent_page_num,
+                     uint32_t child_page_num)
+{
+    /*
+     * Add a new child/key pair to parent that corresponds to child.
+     */
+    void     *right_child;
+    void     *parent = get_page(table->pager, parent_page_num);
+    void     *child = get_page(table->pager, child_page_num);
+    uint32_t  right_child_page_num;
+    uint32_t  child_max_key = get_node_max_key(child);
+    uint32_t  index = internal_node_find_child(parent, child_max_key);
+    uint32_t  original_num_keys = *internal_node_num_keys(parent);
+
+    *internal_node_num_keys(parent) = original_num_keys + 1;
+
+    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
+        printf("Need to implement splitting internal node\n");
+        exit(EXIT_FAILURE);
+    }
```

新 `cell` （孩子/键对）应插入的索引位置取决于新子节点中的最大键。在我们的例子中，`child_max_key` 将是 `5`，索引的位置为 `1`。

如果内部节点没有足够的空间容纳另一个 `cell`，则抛出一个错误。我们稍后会解决这个问题。

现在让我们看看该函数的其余部分：

```diff
+
+    right_child_page_num = *internal_node_right_child(parent);
+    right_child = get_page(table->pager, right_child_page_num);
+
+    if (child_max_key > get_node_max_key(right_child)) {
+        /* Replace right child. */
+        *internal_node_child(parent, original_num_keys) = right_child_page_num;
+        *internal_node_key(parent, original_num_keys) =
+            get_node_max_key(right_child);
+        *internal_node_right_child(parent) = child_page_num;
+    } else {
+        /* Make root for the new cell. */
+        uint32_t i;
+        for (i = original_num_keys; i > index; i--) {
+            void *destination = internal_node_cell(parent, i);
+            void *source = internal_node_cell(parent, i - 1);
+            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
+        }
+        *internal_node_child(parent, index) = child_page_num;
+        *internal_node_key(parent, index) = child_max_key;
+    }
+}
```

因为我们将最右边的孩子的指针与其他的孩子/键对分开存储，如果新的孩子要成为最右边的孩子，我们必须以不同的方式处理。

在我们的例子中，我们会进入 `else` 块。首先，我们通过将其他 `cell` 向右移动一个空格来为新的 `cell` 腾出空间（虽然在我们的例子中，没有 `cell` 需要移动）。

接下来，我们将新的孩子指针和键存储到由 `index` 指向的 `cell` 中。

为了减少所需要的测试案例的大小，我对 `INTERNAL_NODE_MAX_CELLS` 进行了硬编码。

```diff
@@ -163,6 +163,8 @@ const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
 const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
 const uint32_t INTERNAL_NODE_CELL_SIZE =
     INTERNAL_NODE_KEY_SIZE + INTERNAL_NODE_CHILD_SIZE;
+/* Keep this small for testing */
+const uint32_t INTERNAL_NODE_MAX_CELLS = 3;
```

说到测试，我们的大数据集测试将需要更新：

```diff
@@ -65,7 +65,7 @@ describe 'database' do
     result = run_script(script)
     expect(result.last(2)).to match_array([
       "db > Executed.",
-      "db > Need to implement updating parent after split",
+      "db > Need to implement splitting internal node",
     ])
   end
```

我知道，这非常令人满意。

我将添加另一个测试，打印一个四节点的树。只是为了让我们测试更多的情况，而不是连续的 ID，这个测试将以伪随机的顺序添加记录。

```diff
+  it 'allows printing out the structure of a 4-leaf-node btree' do
+    script = [
+      "insert 18 user18 person18@example.com",
+      "insert 7 user7 person7@example.com",
+      "insert 10 user10 person10@example.com",
+      "insert 29 user29 person29@example.com",
+      "insert 23 user23 person23@example.com",
+      "insert 4 user4 person4@example.com",
+      "insert 14 user14 person14@example.com",
+      "insert 30 user30 person30@example.com",
+      "insert 15 user15 person15@example.com",
+      "insert 26 user26 person26@example.com",
+      "insert 22 user22 person22@example.com",
+      "insert 19 user19 person19@example.com",
+      "insert 2 user2 person2@example.com",
+      "insert 1 user1 person1@example.com",
+      "insert 21 user21 person21@example.com",
+      "insert 11 user11 person11@example.com",
+      "insert 6 user6 person6@example.com",
+      "insert 20 user20 person20@example.com",
+      "insert 5 user5 person5@example.com",
+      "insert 8 user8 person8@example.com",
+      "insert 9 user9 person9@example.com",
+      "insert 3 user3 person3@example.com",
+      "insert 12 user12 person12@example.com",
+      "insert 27 user27 person27@example.com",
+      "insert 17 user17 person17@example.com",
+      "insert 16 user16 person16@example.com",
+      "insert 13 user13 person13@example.com",
+      "insert 24 user24 person24@example.com",
+      "insert 25 user25 person25@example.com",
+      "insert 28 user28 person28@example.com",
+      ".btree",
+      ".exit",
+    ]
+    result = run_script(script)
```

它将输出如下结果：

```
- internal (size 3)
  - leaf (size 7)
    - 1
    - 2
    - 3
    - 4
    - 5
    - 6
    - 7
  - key 1
  - leaf (size 8)
    - 8
    - 9
    - 10
    - 11
    - 12
    - 13
    - 14
    - 15
  - key 15
  - leaf (size 7)
    - 16
    - 17
    - 18
    - 19
    - 20
    - 21
    - 22
  - key 22
  - leaf (size 8)
    - 23
    - 24
    - 25
    - 26
    - 27
    - 28
    - 29
    - 30
db >
```

仔细看，你会发现一个错误：

```
    - 5
    - 6
    - 7
  - key 1
```

这里键应该为 `7` 而不是 `1`。

经过调试，我发现这是由于一些错误的指针运算导致的。

```diff
 uint32_t *
 internal_node_key(void *node, uint32_t key_num)
 {
-    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
+    return (void *)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
 }
```

`INTERNAL_NODE_CHILD_SIZE` 是 4. 我的意图是在 `internal_node_cell()` 的结果中增加 4 个字节，但由于 `internal_node_cell()` 返回一个 `uint32_t *`，这实际上是增加了 `4 * sizeof(uint32_t)` 字节。我通过在做算术之前将其转换为一个 `void *` 来解决这个问题。

注意! [void 指针上的指针运算不是 C 标准的一部分，可能不适用于您的编译器](https://stackoverflow.com/questions/3523145/pointer-arithmetic-for-void-pointer-in-c/46238658#46238658). 以后可能会写一篇关于可移植性的文章，但是我现在要将其保留为 `void` 类型指针运算。

好吧。朝着完全可操作的 b-tree 实现又迈进了一步。下一步应该是拆分内部节点。直到那时！

## 完整的 diff

```diff
diff --git a/db.c b/db.c
index c89688a..33d876a 100644
--- a/db.c
+++ b/db.c
@@ -163,6 +163,8 @@ const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
 const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
 const uint32_t INTERNAL_NODE_CELL_SIZE =
     INTERNAL_NODE_KEY_SIZE + INTERNAL_NODE_CHILD_SIZE;
+/* Keep this small for testing */
+const uint32_t INTERNAL_NODE_MAX_CELLS = 3;
 
 void indent(uint32_t level);
 void print_prompt();
@@ -214,12 +216,17 @@ bool is_node_root(void *node);
 void set_node_root(void *node, bool is_root);
 
 void create_new_root(Table *table, uint32_t right_child_page_num);
+uint32_t *node_parent(void *node);
 uint32_t *internal_node_num_keys(void *node);
 uint32_t *internal_node_right_child(void *node);
 uint32_t *internal_node_cell(void *node, uint32_t cell_num);
 uint32_t *internal_node_child(void *node, uint32_t child_num);
 uint32_t *internal_node_key(void *node, uint32_t key_num);
 Cursor *internal_node_find(Table *table, uint32_t page_num, uint32_t key);
+void internal_node_insert(Table *table, uint32_t parent_page_num,
+                          uint32_t child_page_num);
+
+void update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key);
 
 
 void
@@ -790,11 +797,13 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
      */
     int32_t   i;
     void     *old_node = get_page(cursor->table->pager, cursor->page_num);
+    uint32_t  old_max = get_node_max_key(old_node);
     uint32_t  new_page_num = get_unused_page_num(cursor->table->pager);
     void     *new_node = get_page(cursor->table->pager, new_page_num);
 
     initialize_leaf_node(new_node);
 
+    *node_parent(new_node) = *node_parent(old_node);
     *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
     *leaf_node_next_leaf(old_node) = new_page_num;
 
@@ -835,8 +844,12 @@ leaf_node_split_and_insert(Cursor *cursor, uint32_t key, Row *value)
     if (is_node_root(old_node)) {
         return create_new_root(cursor->table, new_page_num);
     } else {
-        printf("Need to implement updating parent after split\n");
-        exit(EXIT_FAILURE);
+        uint32_t   parent_page_num = *node_parent(old_node);
+        uint32_t   new_max = get_node_max_key(old_node);
+        void      *parent = get_page(cursor->table->pager, parent_page_num);
+
+        update_internal_node_key(parent, old_max, new_max);
+        internal_node_insert(cursor->table, parent_page_num, new_page_num);
     }
 }
 
@@ -923,6 +936,14 @@ create_new_root(Table *table, uint32_t right_child_page_num)
     left_child_max_key = get_node_max_key(left_child);
     *internal_node_key(root, 0) = left_child_max_key;
     *internal_node_right_child(root) = right_child_page_num;
+    *node_parent(left_child) = table->root_page_num;
+    *node_parent(right_child) = table->root_page_num;
+}
+
+uint32_t *
+node_parent(void *node)
+{
+    return node + PARENT_POINTER_OFFSET;
 }
 
 uint32_t *
@@ -960,20 +981,21 @@ internal_node_child(void *node, uint32_t child_num)
 uint32_t *
 internal_node_key(void *node, uint32_t key_num)
 {
-    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
+    return (void *)internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
 }
 
-Cursor *
-internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+uint32_t
+internal_node_find_child(void *node, uint32_t key)
 {
-    void     *node = get_page(table->pager, page_num);
-    uint32_t  num_keys = *internal_node_num_keys(node);
-    uint32_t  child_num;
-    void     *child;
+    /*
+     * Return the index of the child which should contain
+     * the given key.
+     */
+    uint32_t num_keys = *internal_node_num_keys(node);
 
-    /* Binary search to find index of child to search. */
-    uint32_t  min_index = 0;
-    uint32_t  max_index = num_keys; /* There is one more child than key. */
+    /* Binary search. */
+    uint32_t min_index = 0;
+    uint32_t max_index = num_keys; /* there is one more child than key */
 
     while (min_index != max_index) {
         uint32_t index = (min_index + max_index) / 2;
@@ -985,8 +1007,16 @@ internal_node_find(Table *table, uint32_t page_num, uint32_t key)
         }
     }
 
-    child_num = *internal_node_child(node, min_index);
-    child = get_page(table->pager, child_num);
+    return min_index;
+}
+
+Cursor *
+internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  child_index = internal_node_find_child(node, key);
+    uint32_t  child_num = *internal_node_child(node, child_index);
+    void     *child = get_page(table->pager, child_num);
     switch (get_node_type(child)) {
     case NODE_LEAF:
         return leaf_node_find(table, child_num, key);
@@ -995,6 +1025,57 @@ internal_node_find(Table *table, uint32_t page_num, uint32_t key)
     }
 }
 
+void
+internal_node_insert(Table *table, uint32_t parent_page_num,
+                     uint32_t child_page_num)
+{
+    /*
+     * Add a new child/key pair to parent that corresponds to child.
+     */
+    void     *right_child;
+    void     *parent = get_page(table->pager, parent_page_num);
+    void     *child = get_page(table->pager, child_page_num);
+    uint32_t  right_child_page_num;
+    uint32_t  child_max_key = get_node_max_key(child);
+    uint32_t  index = internal_node_find_child(parent, child_max_key);
+    uint32_t  original_num_keys = *internal_node_num_keys(parent);
+
+    *internal_node_num_keys(parent) = original_num_keys + 1;
+
+    if (original_num_keys >= INTERNAL_NODE_MAX_CELLS) {
+        printf("Need to implement splitting internal node\n");
+        exit(EXIT_FAILURE);
+    }
+
+    right_child_page_num = *internal_node_right_child(parent);
+    right_child = get_page(table->pager, right_child_page_num);
+
+    if (child_max_key > get_node_max_key(right_child)) {
+        /* Replace right child. */
+        *internal_node_child(parent, original_num_keys) = right_child_page_num;
+        *internal_node_key(parent, original_num_keys) =
+            get_node_max_key(right_child);
+        *internal_node_right_child(parent) = child_page_num;
+    } else {
+        /* Make root for the new cell. */
+        uint32_t i;
+        for (i = original_num_keys; i > index; i--) {
+            void *destination = internal_node_cell(parent, i);
+            void *source = internal_node_cell(parent, i - 1);
+            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
+        }
+        *internal_node_child(parent, index) = child_page_num;
+        *internal_node_key(parent, index) = child_max_key;
+    }
+}
+
+void
+update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key)
+{
+    uint32_t old_child_index = internal_node_find_child(node, old_key);
+    *internal_node_key(node, old_child_index) = new_key;
+}
+
 int
 main(int argc, char *argv[])
 {
```

测试用例。

```diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index f488df6..41f03a9 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -65,7 +65,7 @@ describe 'database' do
     result = run_script(script)
     expect(result.last(2)).to match_array([
       "db > Executed.",
-      "db > Need to implement updating parent after split",
+      "db > Need to implement splitting internal node",
     ])
   end
 
@@ -236,4 +236,85 @@ describe 'database' do
     ])
   end
 
+  it 'allows printing out the structure of a 4-leaf-node btree' do
+    script = [
+      "insert 18 user18 person18@example.com",
+      "insert 7 user7 person7@example.com",
+      "insert 10 user10 person10@example.com",
+      "insert 29 user29 person29@example.com",
+      "insert 23 user23 person23@example.com",
+      "insert 4 user4 person4@example.com",
+      "insert 14 user14 person14@example.com",
+      "insert 30 user30 person30@example.com",
+      "insert 15 user15 person15@example.com",
+      "insert 26 user26 person26@example.com",
+      "insert 22 user22 person22@example.com",
+      "insert 19 user19 person19@example.com",
+      "insert 2 user2 person2@example.com",
+      "insert 1 user1 person1@example.com",
+      "insert 21 user21 person21@example.com",
+      "insert 11 user11 person11@example.com",
+      "insert 6 user6 person6@example.com",
+      "insert 20 user20 person20@example.com",
+      "insert 5 user5 person5@example.com",
+      "insert 8 user8 person8@example.com",
+      "insert 9 user9 person9@example.com",
+      "insert 3 user3 person3@example.com",
+      "insert 12 user12 person12@example.com",
+      "insert 27 user27 person27@example.com",
+      "insert 17 user17 person17@example.com",
+      "insert 16 user16 person16@example.com",
+      "insert 13 user13 person13@example.com",
+      "insert 24 user24 person24@example.com",
+      "insert 25 user25 person25@example.com",
+      "insert 28 user28 person28@example.com",
+      ".btree",
+      ".exit",
+    ]
+    result = run_script(script)
+
+    expect(result[30...(result.length)]).to match_array([
+      "db > Tree:",
+      "- internal (size 3)",
+      "  - leaf (size 7)",
+      "    - 1",
+      "    - 2",
+      "    - 3",
+      "    - 4",
+      "    - 5",
+      "    - 6",
+      "    - 7",
+      "  - key 7",
+      "  - leaf (size 8)",
+      "    - 8",
+      "    - 9",
+      "    - 10",
+      "    - 11",
+      "    - 12",
+      "    - 13",
+      "    - 14",
+      "    - 15",
+      "  - key 15",
+      "  - leaf (size 7)",
+      "    - 16",
+      "    - 17",
+      "    - 18",
+      "    - 19",
+      "    - 20",
+      "    - 21",
+      "    - 22",
+      "  - key 22",
+      "  - leaf (size 8)",
+      "    - 23",
+      "    - 24",
+      "    - 25",
+      "    - 26",
+      "    - 27",
+      "    - 28",
+      "    - 29",
+      "    - 30",
+      "db > ",
+    ])
+  end
+
 end
```
