import re
import jinja2

#/提取结构体
def  extract_structs_from_cpp(file_path):
    structs_keywords=r'struct\s+'
    #structs_modifier = r'(HELEN_ALIGN\s+)?'
    #structs_modifier = r''
    structs_name=r'(\w+)\s*'
    struct_body=r'{((?:.|\n)*?)}'
    #
    pattern = re.compile(
            structs_keywords + structs_name + struct_body,
                         re.DOTALL)
    structs = []
    field_pattern = re.compile(r'\s*(\w+)\s+(\w+)(\[(\d+)\])?;')
    #print(file_path)
    #
    with open(file_path, 'r') as file:
        text = file.read();
        #print(text)
        matchs = pattern.findall(text)
        #print(matchs)
        #
        struct = {}
        for match in matchs:
            #print(match)
            struct['name'] = match[0].strip() 
            struct_content = match[1].strip()
            field_matchs = field_pattern.findall(struct_content)
            fields = []
            for field_type, field_name, s,size in field_matchs:
                #print(f'{field_type} : {field_name} {size}')
                field = {}
                field['type'] = field_type
                field['name'] = field_name
                if  len(size) > 0:
                    field['array'] = int(size)
                fields.append(field)
            struct['fields'] = fields 
            structs.append(struct)
            #structs['structs'].append(struct)
    return structs 

        
            


#/输出模板代码
def  codec_template(data, tpl, out):
    template_loader = jinja2.FileSystemLoader(searchpath="./")
    template_env = jinja2.Environment(loader=template_loader)
    tempalte = template_env.get_template(tpl)
    formatted_data = template.render(structs=data['structs'], file_name=data['file_name'])
    #
    with open(out, 'w') as file:
        file.write(formatted_data)
    
data = extract_structs_from_cpp("example.h")
print(data)



