<?php
// This file is part of Moodle - http://moodle.org/
//
// Moodle is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Moodle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Moodle.  If not, see <http://www.gnu.org/licenses/>.

/**
 * Faces page controller.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

require_once(__DIR__ . '/../../../config.php');

$courseid = required_param('cid', PARAM_INT);
$groupid = optional_param('groupid', 0, PARAM_INT);
$groupids = optional_param_array('groupids', null, PARAM_INT);
if (!is_array($groupids)) {
    $groupidscsv = optional_param('groupids', '', PARAM_SEQUENCE);
    if ($groupidscsv === '') {
        $groupids = [];
    } else {
        $groupids = array_map('intval', preg_split('/\s*,\s*/', $groupidscsv, -1, PREG_SPLIT_NO_EMPTY));
    }
} else {
    $groupids = array_map('intval', $groupids);
}
$orderby = optional_param('orderby', 'firstname', PARAM_ALPHANUMEXT);

$course = get_course($courseid);
require_login($course);
$context = context_course::instance($course->id);
require_capability('block/faces:view', $context);

$url = new moodle_url('/blocks/faces/showfaces/show.php', [
    'cid' => $course->id,
    'groupid' => $groupid,
    'orderby' => $orderby,
]);
if (!empty($groupids)) {
    \block_faces\local\groups_helper::apply_groupids_to_url($url, $groupids);
}

$PAGE->set_url($url);
$PAGE->set_context($context);
$PAGE->set_heading(format_string($course->fullname, true, ['context' => $context]));
$PAGE->set_title(get_string('pluginname', 'block_faces'));
$PAGE->set_pagelayout('course');

$PAGE->navbar->add(format_string($course->shortname, true, ['context' => $context]),
    new moodle_url('/course/view.php', ['id' => $course->id]));
$PAGE->navbar->add(get_string('showallfaces', 'block_faces'));

$renderable = new \block_faces\output\faces_page($course, $groupid, $orderby, $groupids, true);

echo $OUTPUT->header();
echo $OUTPUT->render($renderable);
echo $OUTPUT->footer();
