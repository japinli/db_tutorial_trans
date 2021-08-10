---
title: 11 - B-Tree 的递归查找
---

在上一篇文章中，我们以插入第 15 行的错误结束：

```
db > insert 15 user15 person15@example.com
Need to implement searching an internal node
```

首先，我们使用新的函数替换原有代码。

```diff
     if (get_node_type(root_node) == NODE_LEAF) {
         return leaf_node_find(table, root_page_num, key);
     } else {
-        printf("Need to implement searching an internal node\n");
-        exit(EXIT_FAILURE);
+        return internal_node_find(table, root_page_num, key);
     }
 }
```

此函数将执行二分搜索以查找应包含给定键的子项。记住，每个子项指针右边的键是该子项包含的最大键。

{% include image.html url="assets/images/btree6.png" description="三层 btree" %}

所以我们的二分查找比较了要查找的键和子指针右边的键：

```diff
+Cursor *
+internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  num_keys = *internal_node_num_keys(node);
+    uint32_t  child_num;
+    void     *child;
+
+    /* Binary search to find index of child to search. */
+    uint32_t  min_index = 0;
+    uint32_t  max_index = num_keys; /* There is one more child than key. */
+
+    while (min_index != max_index) {
+        uint32_t index = (min_index + max_index) / 2;
+        uint32_t key_to_right = *internal_node_key(node, index);
+        if (key_to_right >= key) {
+            max_index = index;
+        } else {
+            min_index = index + 1;
+        }
+    }
```

还要记住，内部节点的子节点可以是叶节点或更多内部节点。在我们找到正确的孩子之后，对其调用相应的搜索函数：

```diff
+    child_num = *internal_node_child(node, min_index);
+    child = get_page(table->pager, child_num);
+    switch (get_node_type(child)) {
+    case NODE_LEAF:
+        return leaf_node_find(table, child_num, key);
+    case NODE_INTERNAL:
+        return internal_node_find(table, child_num, key);
+    }
+}
```

# 测试用例

现在，将键插入多节点 btree 将不再导致错误。我们需要更新我们的测试：

```diff
       "    - 12",
       "    - 13",
       "    - 14",
-      "db > Need to implement searching an internal node",
+      "db > Executed.",
+      "db > ",
     ])
   end
```

我还认为现在是我们重新审视另一个测试用例的时候了。那个测试尝试插入 1400 行的测试用例。它仍然会出错，但是错误信息是新的。现在，当程序崩溃时，我们的测试并不能很好地处理它。如果发生这种情况，我们就用目前得到的输出结果：

```diff
     raw_output = nil
     IO.popen("./db test.db", "r+") do |pipe|
       commands.each do |command|
-        pipe.puts command
+        begin
+          pipe.puts command
+        rescue Errno::EPIPE
+          break
+        end
       end

       pipe.close_write
```

这表明我们的 1400 行测试输出这个错误：

```diff
     end
     script << ".exit"
     result = run_script(script)
-    expect(result[-2]).to eq('db > Error: Table full.')
+    expect(result.last(2)).to match_array([
+      "db > Executed.",
+      "db > Need to implement updating parent after split",
+    ])
   end
```

看起来这是我们待办事项清单上的下一个！

## 译者著

由于在上篇文章中，我们将 1400 行这个测试用例给删除了，因此在本篇中需要加上：

```diff
+  it 'prints error message when table is full' do
+    script = (1..1401).map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".exit"
+    result = run_script(script)
+    expect(result.last(2)).to match_array([
+      "db > Executed.",
+      "db > Need to implement updating parent after split",
+    ])
+  end
```

## 完整的 diff

```diff
diff --git a/db.c b/db.c
index 4a356bb..e3fb7b3 100644
--- a/db.c
+++ b/db.c
@@ -215,6 +215,7 @@ uint32_t *internal_node_right_child(void *node);
 uint32_t *internal_node_cell(void *node, uint32_t cell_num);
 uint32_t *internal_node_child(void *node, uint32_t child_num);
 uint32_t *internal_node_key(void *node, uint32_t key_num);
+Cursor *internal_node_find(Table *table, uint32_t page_num, uint32_t key);
 
 
 void
@@ -647,8 +648,7 @@ table_find(Table *table, uint32_t key)
     if (get_node_type(root_node) == NODE_LEAF) {
         return leaf_node_find(table, root_page_num, key);
     } else {
-        printf("Need to implement searching an internal node\n");
-        exit(EXIT_FAILURE);
+        return internal_node_find(table, root_page_num, key);
     }
 }
 
@@ -946,6 +946,38 @@ internal_node_key(void *node, uint32_t key_num)
     return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
 }
 
+Cursor *
+internal_node_find(Table *table, uint32_t page_num, uint32_t key)
+{
+    void     *node = get_page(table->pager, page_num);
+    uint32_t  num_keys = *internal_node_num_keys(node);
+    uint32_t  child_num;
+    void     *child;
+
+    /* Binary search to find index of child to search. */
+    uint32_t  min_index = 0;
+    uint32_t  max_index = num_keys; /* There is one more child than key. */
+
+    while (min_index != max_index) {
+        uint32_t index = (min_index + max_index) / 2;
+        uint32_t key_to_right = *internal_node_key(node, index);
+        if (key_to_right >= key) {
+            max_index = index;
+        } else {
+            min_index = index + 1;
+        }
+    }
+
+    child_num = *internal_node_child(node, min_index);
+    child = get_page(table->pager, child_num);
+    switch (get_node_type(child)) {
+    case NODE_LEAF:
+        return leaf_node_find(table, child_num, key);
+    case NODE_INTERNAL:
+        return internal_node_find(table, child_num, key);
+    }
+}
+
 int
 main(int argc, char *argv[])
 {
```

测试用例：

```diff
diff --git a/spec/main_spec.rb b/spec/main_spec.rb
index 4cf293f..5ed35b0 100644
--- a/spec/main_spec.rb
+++ b/spec/main_spec.rb
@@ -7,7 +7,11 @@ describe 'database' do
     raw_output = nil
     IO.popen("./db test.db", "r+") do |pipe|
       commands.each do |command|
-        pipe.puts command
+        begin
+          pipe.puts command
+        rescue Errno::EPIPE
+          break
+        end
       end
 
       pipe.close_write
@@ -53,6 +57,18 @@ describe 'database' do
     ])
   end
 
+  it 'prints error message when table is full' do
+    script = (1..1401).map do |i|
+      "insert #{i} user#{i} person#{i}@example.com"
+    end
+    script << ".exit"
+    result = run_script(script)
+    expect(result.last(2)).to match_array([
+      "db > Executed.",
+      "db > Need to implement updating parent after split",
+    ])
+  end
+
   it 'allows inserting strings that are the maximum length' do
     long_username = "a"*32
     long_email = "a"*255
@@ -186,7 +202,8 @@ describe 'database' do
       "    - 12",
       "    - 13",
       "    - 14",
-      "db > Need to implement searching an internal node",
+      "db > Executed.",
+      "db > ",
     ])
   end
```
