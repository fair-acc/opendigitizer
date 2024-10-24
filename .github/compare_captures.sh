#!/bin/bash

# Implements the screen capture comparison (task #213), the algorthm is follows:
# 1. ctest runs the tests which produce screenshots in the build directory (ran by the build_cmake.yml)
# 2. This script is called, to compare if the resulting screenshots differ from the reference ones
#  - The reference images are downloaded
#  - All PR images are diffed against reference images
#  - A GitHub PR comment is created listing all different/missing/new images
# 3. upload-reference-screencaptures.yml is triggered on PR merge, it will refresh the reference images
#
# As an implementation detail, we're using "GH releases" as our storage, since "upload/artifacts" has limit
# of 90 days, 500MB storage limit and isn't available via 'gh' commandline tool.
# We're using 2 dummy releases to store assets: 'reference_screen_captures-main' and 'test_screen_captures'
# , the latter stores diffs, so they can be linked from in PR comments.

if [ -z "$GITHUB_EVENT_NAME" ]; then
    echo "Error: this script is expected to only run under GitHub actions";
    exit 1
fi

if [ "$#" -ne 4 ] ; then
    echo "Usage: compare_captures.sh <PR_NUMBER> <REPO_NAME> <reference_capture_dir> <current_capture_dir>"
    exit 1
fi

PR_NUMBER=$1
REPO_NAME=$2
REFERENCE_CAPTURES_DIR=$3
PR_CAPTURES_DIR=$4
DIFF_DIR=$PR_CAPTURES_DIR/../diffs/
DIFFS_RELEASE_NAME=test_screen_captures
REFERENCE_RELEASE_NAME=reference_screen_captures-main

mkdir "$DIFF_DIR" &> /dev/null

# make *.png expand to empty if there's no png file

setopt nullglob  &> /dev/null # zsh
shopt -s nullglob &> /dev/null # bash

# Download reference captures
gh release download $REFERENCE_RELEASE_NAME -p "*.png" -D "$REFERENCE_CAPTURES_DIR"

# Let's accumulate the results in these arrays
# so we can print them in one go in a single PR comment if we want

images_with_differences=()
new_images_in_pr=()
images_missing_in_pr=()

for i in ${PR_CAPTURES_DIR}/*.png ; do
    image_name=$(basename "$i")
    reference_image=$REFERENCE_CAPTURES_DIR/$image_name

    echo "Testing $image_name"

    if [[ -f $reference_image ]] ; then
        # 'compare' from GH's old Ubuntu always returns 1 even if files are the same. Guard with 'diff'
        if ! diff "$PR_CAPTURES_DIR"/"$image_name" "$reference_image" &> /dev/null ; then
            echo "Found differences for $image_name"
            compare -compose src "$PR_CAPTURES_DIR"/"$image_name" "$reference_image" "$DIFF_DIR"/"${PR_NUMBER}"-"${image_name}"_diff.png
            images_with_differences+=($image_name)

            # we'll be uploading it so we can link it from PR, copy to diff dir
            cp "$PR_CAPTURES_DIR"/"$image_name" "$DIFF_DIR"/"${PR_NUMBER}"-"${image_name}"
        fi
    else
        echo "Found new image $image_name"
        new_images_in_pr+=($image_name)

        # we'll upload the new image as well, copy to diff dir
        cp "$PR_CAPTURES_DIR"/"$image_name" "$DIFF_DIR"/"${PR_NUMBER}"-"${image_name}"
    fi
done

# check if there's any images missing in PR
for i in ${REFERENCE_CAPTURES_DIR}/*.png ; do
    image_name=$(basename "$i")
    pr_image=$PR_CAPTURES_DIR/$image_name

    if [ ! -f "$pr_image" ] ; then
        echo "Could not find $image_name in PR"
        images_missing_in_pr+=$image_name
    fi
done

if [[ ${#images_with_differences[@]} -eq 0 && ${#new_images_in_pr[@]} -eq 0 && ${#images_missing_in_pr[@]} -eq 0 ]]; then
    # Still useful to show a comment on success, in case PR has previous diff comments
    gh pr comment "$PR_NUMBER" --edit-last --body "✅ No screencapture diffs to report!"

    # All is good now, when we merge, do not upload anything to reference_screen_captures
    gh release delete-asset test_screen_captures "${PR_NUMBER}"-all-captures.tgz

    exit 0
fi

# Make sure the asset releases exist

if ! gh release list | grep -q "$DIFFS_RELEASE_NAME"  ; then
    echo "No asset release for diffs, creating..."
    gh release create ${DIFFS_RELEASE_NAME} --notes "Screen captures diffs for faulty pull requests"
fi

if ! gh release list | grep -q "$REFERENCE_RELEASE_NAME"  ; then
    echo "No asset release for reference capture, creating..."
    gh release create ${REFERENCE_RELEASE_NAME} --notes "Reference screen captures"
fi

if [ -n "$(ls -A "$DIFF_DIR")" ]; then # if not-empty
    echo "Uploading diffs..."
    gh release upload ${DIFFS_RELEASE_NAME} "$DIFF_DIR"/*png --clobber || exit 1
fi

tar cvzf "${PR_NUMBER}"-all-captures.tgz -C "$(dirname "$PR_CAPTURES_DIR")" "$(basename "$PR_CAPTURES_DIR")"

# Once the PR gets merged we need to access this tgz as it will be the new reference
echo "Uploading all PR captures..."
gh release upload ${DIFFS_RELEASE_NAME} "${PR_NUMBER}"-all-captures.tgz --clobber || exit 1

pr_text=""

if [[ ${#images_with_differences[@]} -ne 0 ]] ; then
    pr_text+="# PR produced different images:\n\n"
    for i in "${images_with_differences[@]}" ; do
        pr_text+="<details>\n"
        pr_text+="<summary>$i</summary>\n"
        pr_text+="\n### Got:\n ![$i](https://github.com/${REPO_NAME}/releases/download/${DIFFS_RELEASE_NAME}/${PR_NUMBER}-${i}) \n"
        pr_text+="\n### Expected:\n ![$i](https://github.com/${REPO_NAME}/releases/download/${REFERENCE_RELEASE_NAME}/${i}) \n"
        pr_text+="\n### Diff:\n ![$i](https://github.com/${REPO_NAME}/releases/download/${DIFFS_RELEASE_NAME}/${PR_NUMBER}-${i}_diff.png) \n"
        pr_text+="</details>\n"
    done
fi

if [[ ${#new_images_in_pr[@]} -ne 0 ]] ; then
    pr_text+="\n# PR has new images:\n\n"
    for i in "${new_images_in_pr[@]}" ; do
        pr_text+="<details>\n\n"
        pr_text+="<summary>$i</summary>\n"
        pr_text+="<img src=\"https://github.com/${REPO_NAME}/releases/download/${DIFFS_RELEASE_NAME}/${PR_NUMBER}-${i}\" style=\"max-width: 50%; height: auto;\" >"
        pr_text+="</details>\n"
    done
fi

if [[ ${#images_missing_in_pr[@]} -ne 0 ]] ; then
    pr_text+="\n# PR didn't produce the following images:\n\n"
    for i in "${images_missing_in_pr[@]}" ; do
        pr_text+="<details>\n\n"
        pr_text+="<summary>$i</summary>\n"
        pr_text+="<img src=\"https://github.com/${REPO_NAME}/releases/download/${REFERENCE_RELEASE_NAME}/${i}\" style=\"max-width: 50%; height: auto;\" >"
        pr_text+="</details>"
    done
fi

if [[ -z "$pr_text" ]]; then
    # All files are the same
    rmdir "$DIFF_DIR"
else
    formatted_text=$(echo -e "$pr_text") # expand \n

    echo "Creating PR comment with content"
    echo -e "$formatted_text"

    gh pr comment "$PR_NUMBER" --body "$formatted_text"
fi
