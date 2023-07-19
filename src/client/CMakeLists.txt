# 定义了一个SRC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. SRC_LIST)

# 指定生成可执行文件ChatClient，由SRC_LIST变量所定义的源文件编译而来 
add_executable(ChatClient ${SRC_LIST})

# 指定可执行文件ChatClient链接时需要依赖的库文件pthread
target_link_libraries(ChatClient pthread)