import argparse
import argcomplete
import sys
import os
import copy
import pickle
from pathlib import Path
from nntool.api import NNGraph
from nntool.utils.stats_funcs import qsnr
from PIL import Image
import numpy as np

def create_parser():
    # create the top-level parser
    parser = argparse.ArgumentParser(prog='train')

    parser.add_argument('--trained_model', default=None, required=True,
                        help='Output - Trained model in tflite format')
    parser.add_argument('--insert_resizer', action="store_true",
                        help='Put resizer in front of the NN to take images from the camera')

    # Generate mode options
    parser.add_argument('--tensors_dir', default="tensors",
                        help="Where nntool stores the weights/bias tensors dir (only used in generate and performance mode)")
    parser.add_argument('--at_model_path', default=None,
                        help="Path to the C autotiler model file to generate (only used in generate mode)")
    parser.add_argument('--ram_type', default="AT_MEM_L3_DEFAULTRAM", choices=['AT_MEM_L3_HRAM', 'AT_MEM_L3_QSPIRAM', 'AT_MEM_L3_OSPIRAM', 'AT_MEM_L3_DEFAULTRAM'],
                        help="Ram type to use during inference on platform (only used in generate and performance mode)")
    parser.add_argument('--flash_type', default="AT_MEM_L3_DEFAULTFLASH", choices=['AT_MEM_L3_HFLASH', 'AT_MEM_L3_QSPIFLASH', 'AT_MEM_L3_OSPIFLASH', 'AT_MEM_L3_MRAMFLASH', 'AT_MEM_L3_DEFAULTFLASH'],
                        help="Flash type to use during inference (only used in generate and performance mode)")
    return parser

if __name__ == '__main__':
 
    parser = create_parser()
    argcomplete.autocomplete(parser)
    args = parser.parse_args()

    G = NNGraph.load_graph(args.trained_model, load_quantization=True)
    G.name = "vww"
    G.adjust_order()
    G.fusions("scaled_match_group")
    G.quantize(
        None,
        graph_options={
            "use_ne16": True,
            "hwc": True
        },
    )
    if args.insert_resizer:
        G.insert_resizer(
            "input_1", (120, 160), "bilinear"
        )

    print(G.show([G[0]]))
    print(G.qshow([G[0]]))
    #G.draw()

    G[0].allocate = True

    G.generate(
        write_constants=True,
        settings={
            "tensor_directory": args.tensors_dir,
            "model_directory": os.path.split(args.at_model_path)[0] if args.at_model_path else "",
            "model_file": os.path.split(args.at_model_path)[1] if args.at_model_path else "ATmodel.c",

            "l1_size": 128000,
            "l2_size": 1000000,

            "graph_monitor_cycles": True,
            "graph_produce_node_names": True,
            "graph_produce_operinfos": True,
            "graph_const_exec_from_flash": True,

            "graph_l1_promotion": 1,

            "l3_ram_device": args.ram_type,
            "l3_flash_device": args.flash_type, #"AT_MEM_L3_DEFAULTFLASH",
        }
    )