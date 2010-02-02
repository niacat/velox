/* velox: velox/keybinding.c
 *
 * Copyright (c) 2009, 2010 Michael Forney <michael@obberon.com>
 *
 * This file is a part of velox.
 *
 * velox is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2, as published by the Free
 * Software Foundation.
 *
 * velox is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along
 * with velox.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>
#include <assert.h>
#include <X11/Xlib.h> // For XStringToKeysym

#include <libvelox/hashtable.h>

#include "keybinding.h"
#include "tag.h"
#include "config_file.h"
#include "velox.h"

#define ADD_TAG_KEY_BINDINGS(N) \
    add_configured_key_binding("tag", STRING_SYMBOL(set_tag_ ## N), NULL); \
    add_configured_key_binding("tag", STRING_SYMBOL(move_focus_to_tag_ ## N), NULL);

#define STRING_SYMBOL(name) #name, &name

static const uint32_t mod_mask = XCB_MOD_MASK_4;

struct velox_hashtable * configured_keys = NULL;
struct velox_list * key_bindings = NULL;

uint16_t modifier_value(const char * name)
{
    if (strcmp(name, "mod_shift") == 0)         return XCB_MOD_MASK_SHIFT;
    else if (strcmp(name, "mod_lock") == 0)     return XCB_MOD_MASK_LOCK;
    else if (strcmp(name, "mod_control") == 0)  return XCB_MOD_MASK_CONTROL;
    else if (strcmp(name, "mod_1") == 0)        return XCB_MOD_MASK_1;
    else if (strcmp(name, "mod_2") == 0)        return XCB_MOD_MASK_2;
    else if (strcmp(name, "mod_3") == 0)        return XCB_MOD_MASK_3;
    else if (strcmp(name, "mod_4") == 0)        return XCB_MOD_MASK_4;
    else if (strcmp(name, "mod_5") == 0)        return XCB_MOD_MASK_5;
    else if (strcmp(name, "mod_any") == 0)      return XCB_MOD_MASK_ANY;

    return 0;
}

void setup_configured_keys()
{
    FILE * file;
    char identifier[256];
    struct velox_key * key;
    struct velox_list * keys;

    yaml_parser_t parser;
    yaml_document_t document;

    configured_keys = velox_hashtable_create(1024, &sdbm_hash);

    /* Look for and open keys.yaml in the standard configuration directories */
    file = open_config_file("keys.yaml");

    assert(file);

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, file);
    if (yaml_parser_load(&parser, &document))
    {
        yaml_node_t * root_map;

        yaml_node_pair_t * root_pair;
        yaml_node_t * root_key, * root_value;

        root_map = document.nodes.start;
        assert(root_map->type == YAML_MAPPING_NODE);

        /* For each key section */
        for (root_pair = root_map->data.mapping.pairs.start;
            root_pair < root_map->data.mapping.pairs.top;
            ++root_pair)
        {
            root_key = yaml_document_get_node(&document, root_pair->key);
            root_value = yaml_document_get_node(&document, root_pair->value);

            assert(root_key->type == YAML_SCALAR_NODE);
            assert(root_value->type == YAML_MAPPING_NODE);

            yaml_node_pair_t * set_pair;
            yaml_node_t * set_key, * set_value;

            /* For each set of key mappings */
            for (set_pair = root_value->data.mapping.pairs.start;
                set_pair < root_value->data.mapping.pairs.top;
                ++set_pair)
            {
                yaml_node_item_t * mapping_item;
                yaml_node_t * mapping_node;

                set_key = yaml_document_get_node(&document, set_pair->key);
                set_value = yaml_document_get_node(&document, set_pair->value);

                assert(set_key->type == YAML_SCALAR_NODE);
                assert(set_value->type == YAML_SEQUENCE_NODE);

                keys = NULL;
                snprintf(identifier, sizeof(identifier), "%s:%s",
                    root_key->data.scalar.value,
                    set_key->data.scalar.value
                );

                /* For each key mapping */
                for (mapping_item = set_value->data.sequence.items.start;
                    mapping_item < set_value->data.sequence.items.top;
                    ++mapping_item)
                {
                    yaml_node_pair_t * key_pair;
                    yaml_node_t * key_key, * key_value;

                    mapping_node = yaml_document_get_node(&document, *mapping_item);

                    assert(mapping_node->type == YAML_MAPPING_NODE);

                    key = (struct velox_key *) malloc(sizeof(struct velox_key));

                    /* Identify key */
                    for (key_pair = mapping_node->data.mapping.pairs.start;
                        key_pair < mapping_node->data.mapping.pairs.top;
                        ++key_pair)
                    {
                        key_key = yaml_document_get_node(&document, key_pair->key);
                        key_value = yaml_document_get_node(&document, key_pair->value);

                        assert(key_key->type == YAML_SCALAR_NODE);

                        if (strcmp((const char const *) key_key->data.scalar.value, "mod") == 0)
                        {
                            yaml_node_item_t * mod_item;
                            yaml_node_t * mod_node;

                            key->modifiers = 0;

                            assert(key_value->type == YAML_SEQUENCE_NODE);

                            for (mod_item = key_value->data.sequence.items.start;
                                mod_item < key_value->data.sequence.items.top;
                                ++mod_item)
                            {
                                mod_node = yaml_document_get_node(&document, *mod_item);
                                assert(mod_node->type == YAML_SCALAR_NODE);
                                key->modifiers |= modifier_value((const char const *) mod_node->data.scalar.value);
                            }
                        }
                        else if (strcmp((const char const *) key_key->data.scalar.value, "key") == 0)
                        {
                            assert(key_value->type == YAML_SCALAR_NODE);
                            key->keysym = XStringToKeysym((const char const *) key_value->data.scalar.value);
                        }
                    }

                    printf("%s: modifiers: %i, keysym: %x\n", identifier, key->modifiers, key->keysym);
                    keys = velox_list_insert(keys, key);
                }

                assert(!velox_hashtable_exists(configured_keys, identifier));
                velox_hashtable_insert(configured_keys, identifier, keys);
            }
        }

        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
    }
    else
    {
        printf("YAML error: %s\n", parser.problem);
    }

    fclose(file);
}

void setup_key_bindings()
{
    /* Window focus */
    add_configured_key_binding("velox", STRING_SYMBOL(focus_next), NULL);
    add_configured_key_binding("velox", STRING_SYMBOL(focus_previous), NULL);
    add_configured_key_binding("velox", STRING_SYMBOL(move_next), NULL);
    add_configured_key_binding("velox", STRING_SYMBOL(move_previous), NULL);

    /* Window operations */
    add_configured_key_binding("velox", STRING_SYMBOL(kill_focused_window), NULL);

    /* Layout control */
    add_configured_key_binding("velox", STRING_SYMBOL(next_layout), NULL);
    add_configured_key_binding("velox", STRING_SYMBOL(previous_layout), NULL);

    /* Quit */
    add_configured_key_binding("velox", STRING_SYMBOL(quit), NULL);

    /* Tags */
    ADD_TAG_KEY_BINDINGS(1)
    ADD_TAG_KEY_BINDINGS(2)
    ADD_TAG_KEY_BINDINGS(3)
    ADD_TAG_KEY_BINDINGS(4)
    ADD_TAG_KEY_BINDINGS(5)
    ADD_TAG_KEY_BINDINGS(6)
    ADD_TAG_KEY_BINDINGS(7)
    ADD_TAG_KEY_BINDINGS(8)
    ADD_TAG_KEY_BINDINGS(9)
}

void cleanup_key_bindings()
{
    key_bindings = velox_list_delete(key_bindings, true);
}

void add_key_binding(struct velox_key * key, void (* function)(void * arg), void * arg)
{
    struct velox_key_binding * binding;

    /* Allocate a new key binding and set its values */
    binding = (struct velox_key_binding *) malloc(sizeof(struct velox_key_binding));
    binding->key = key;
    binding->keycode = 0;
    binding->function = function;
    binding->arg = arg;

    key_bindings = velox_list_insert(key_bindings, binding);
}

void add_configured_key_binding(const char * group, const char * name, void (* function)(void * arg), void * arg)
{
    struct velox_list * keys;
    struct velox_list * iterator;
    char identifier[strlen(group) + strlen(name) + 1];

    if (configured_keys == NULL)
    {
        return;
    }

    sprintf(identifier, "%s:%s", group, name);

    /* Lookup the list of keys associated with that binding in the
     * configured_keys table */
    keys = velox_hashtable_lookup(configured_keys, identifier);

    assert(keys);

    for (iterator = keys; iterator != NULL; iterator = iterator->next)
    {
        add_key_binding((struct velox_key *) iterator->data, function, arg);
    }
}

// vim: fdm=syntax fo=croql et sw=4 sts=4 ts=8

